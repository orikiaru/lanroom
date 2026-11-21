use crate::nic::{Beacon, Nic, Peer};
use crate::transfer::{self, PORT};
use serde::Serialize;
use std::fs::File;
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::{self, Sender};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter};

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PeerView {
	pub mac: String,
	pub source: String,
	pub virtual_ip: String,
	pub direct: bool,
	pub myself: bool,
	pub place: String,
	pub live: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RoomState {
	pub phase: String,
	pub virtual_ip: String,
	pub tun: String,
	pub message: String,
	pub download_dir: String,
	pub peers: Vec<PeerView>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TransferView {
	pub id: String,
	pub direction: String,
	pub name: String,
	pub peer: String,
	pub done: u64,
	pub total: u64,
	pub bytes_per_second: u64,
	pub state: String,
	pub message: String,
}

enum Cmd {
	Send { peer: String, paths: Vec<String> },
	Cancel { id: String },
	Leave,
	SetDir { path: String },
}

struct Running {
	tx: Sender<Cmd>,
	thread: Option<JoinHandle<()>>,
	done: Arc<AtomicBool>,
}

pub struct Room {
	running: Mutex<Option<Running>>,
	download_dir: Arc<Mutex<PathBuf>>,
}

impl Room {
	pub fn new() -> Self {
		let dir = dirs::download_dir().unwrap_or_else(std::env::temp_dir).join("Lanroom");
		Self {
			running: Mutex::new(None),
			download_dir: Arc::new(Mutex::new(dir)),
		}
	}

	pub fn download_dir(&self) -> String {
		self.download_dir.lock().expect("download dir").display().to_string()
	}

	pub fn set_download_dir(&self, path: String) -> Result<(), String> {
		if path.is_empty() {
			return Err("没有选择文件夹".into());
		}
		*self.download_dir.lock().expect("download dir") = PathBuf::from(&path);
		if let Some(running) = self.running.lock().expect("room").as_ref() {
			let _ = running.tx.send(Cmd::SetDir { path });
		}
		Ok(())
	}

	pub fn join(&self, app: AppHandle, server: String, port: u16, room: String, bind: String, token: String) -> Result<(), String> {
		let room = room.trim().to_string();
		if room.is_empty() {
			return Err("房间名不能为空".into());
		}
		if server.trim().is_empty() {
			return Err("服务器地址不能为空".into());
		}
		let mut guard = self.running.lock().expect("room");
		if guard.as_ref().is_some_and(|running| !running.done.load(Ordering::Relaxed)) {
			return Err("已经在加入或已在房间里".into());
		}
		let (tx, rx) = mpsc::channel();
		let download_dir = Arc::clone(&self.download_dir);
		let done = Arc::new(AtomicBool::new(false));
		let done_flag = Arc::clone(&done);
		let thread = thread::spawn(move || {
			worker(app, rx, download_dir, server, port, room, bind, token);
			done_flag.store(true, Ordering::Relaxed);
		});
		*guard = Some(Running { tx, thread: Some(thread), done });
		Ok(())
	}

	pub fn leave(&self) {
		let running = self.running.lock().expect("room").take();
		if let Some(running) = running {
			let _ = running.tx.send(Cmd::Leave);
			if let Some(thread) = running.thread {
				let _ = thread.join();
			}
		}
	}

	pub fn send(&self, peer: String, paths: Vec<String>) -> Result<(), String> {
		let guard = self.running.lock().expect("room");
		let running = guard.as_ref().ok_or("还没有加入房间")?;
		running.tx.send(Cmd::Send { peer, paths }).map_err(|_| "房间已经离开".to_string())
	}

	pub fn cancel(&self, id: String) -> Result<(), String> {
		let guard = self.running.lock().expect("room");
		let running = guard.as_ref().ok_or("还没有加入房间")?;
		running.tx.send(Cmd::Cancel { id }).map_err(|_| "房间已经离开".to_string())
	}
}

fn repo_root() -> PathBuf {
	PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn client_launch() -> Result<(PathBuf, PathBuf, PathBuf), String> {
	let mut candidates = Vec::new();
	if let Ok(exe) = std::env::current_exe() {
		if let Some(dir) = exe.parent() {
			candidates.push(dir.to_path_buf());
		}
	}
	candidates.push(repo_root().join("build"));
	for dir in candidates {
		for name in ["lanroom-client", "lanroom-client.exe"] {
			let binary = dir.join(name);
			let beside = dir.join("client.toml");
			let repo_config = repo_root().join("config").join("client.toml");
			let config = if beside.is_file() { beside } else { repo_config };
			if binary.is_file() && config.is_file() {
				return Ok((binary, config, dir));
			}
		}
	}
	Err("找不到 build/lanroom-client，请先编译客户端".into())
}

fn mac_address() -> Result<String, String> {
	let path = dirs::data_local_dir().unwrap_or_else(std::env::temp_dir).join("lanroom").join("mac");
	if let Ok(text) = std::fs::read_to_string(&path) {
		let text = text.trim();
		if text.len() == 17 && text.starts_with("02:") {
			return Ok(text.to_string());
		}
	}
	let mut bytes = [0u8; 5];
	getrandom::fill(&mut bytes).map_err(|err| err.to_string())?;
	let mac = format!("02:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
	if let Some(parent) = path.parent() {
		let _ = std::fs::create_dir_all(parent);
	}
	std::fs::write(&path, &mac).map_err(|err| err.to_string())?;
	Ok(mac)
}

fn free_port() -> std::io::Result<u16> {
	let listener = TcpListener::bind("127.0.0.1:0")?;
	Ok(listener.local_addr()?.port())
}

fn emit_room(app: &AppHandle, state: &RoomState) {
	let _ = app.emit("room://state", state);
}

fn emit_transfer(app: &AppHandle, view: &TransferView) {
	let _ = app.emit("transfer://update", view);
}

fn idle_state(dir: &str) -> RoomState {
	RoomState {
		phase: "idle".into(),
		virtual_ip: String::new(),
		tun: String::new(),
		message: String::new(),
		download_dir: dir.to_string(),
		peers: Vec::new(),
	}
}

struct Meter {
	last: Instant,
	last_done: u64,
	rate: u64,
}

impl Meter {
	fn new() -> Self {
		Self { last: Instant::now(), last_done: 0, rate: 0 }
	}

	fn update(&mut self, done: u64) -> u64 {
		let elapsed = self.last.elapsed();
		if elapsed >= Duration::from_millis(200) {
			let delta = done.saturating_sub(self.last_done);
			self.rate = (delta as f64 / elapsed.as_secs_f64()) as u64;
			self.last = Instant::now();
			self.last_done = done;
		}
		self.rate
	}
}

fn worker(app: AppHandle, rx: mpsc::Receiver<Cmd>, download_dir: Arc<Mutex<PathBuf>>, server: String, port: u16, room: String, bind: String, token: String) {
	let dir_text = download_dir.lock().expect("dir").display().to_string();
	emit_room(
		&app,
		&RoomState {
			phase: "joining".into(),
			virtual_ip: String::new(),
			tun: String::new(),
			message: "如果系统询问管理员权限，请允许。".into(),
			download_dir: dir_text,
			peers: Vec::new(),
		},
	);
	let result = (|| {
		let (binary, config, workdir) = client_launch()?;
		let mac = mac_address()?;
		let nic_port = free_port().map_err(|err| err.to_string())?;
		let bind = if bind.trim().is_empty() { "0.0.0.0".to_string() } else { bind };
		let mut child = Command::new(&binary)
			.current_dir(&workdir)
			.arg("--config")
			.arg(&config)
			.arg("--room")
			.arg(&room)
			.arg("--mac")
			.arg(&mac)
			.arg("--server")
			.arg(server.trim())
			.arg("--server-port")
			.arg(port.to_string())
			.arg("--bind")
			.arg(bind)
			.arg("--nic-port")
			.arg(nic_port.to_string())
			.arg("--token")
			.arg(&token)
			.arg("--tun")
			.stdin(Stdio::null())
			.stdout(Stdio::inherit())
			.stderr(Stdio::inherit())
			.spawn()
			.map_err(|err| format!("启动客户端失败: {err}"))?;
		run_session(&app, &rx, &download_dir, &mut child, nic_port)
	})();
	if let Err(err) = result {
		let dir_text = download_dir.lock().expect("dir").display().to_string();
		emit_room(
			&app,
			&RoomState {
				phase: "error".into(),
				virtual_ip: String::new(),
				tun: String::new(),
				message: err,
				download_dir: dir_text,
				peers: Vec::new(),
			},
		);
	}
}

fn run_session(app: &AppHandle, rx: &mpsc::Receiver<Cmd>, download_dir: &Arc<Mutex<PathBuf>>, child: &mut Child, nic_port: u16) -> Result<(), String> {
	let deadline = Instant::now() + Duration::from_secs(90);
	let mut nic = loop {
		if let Ok(cmd) = rx.try_recv() {
			if matches!(cmd, Cmd::Leave) {
				let _ = child.kill();
				let _ = child.wait();
				let dir = download_dir.lock().expect("dir").display().to_string();
				emit_room(app, &idle_state(&dir));
				return Ok(());
			}
		}
		if let Some(status) = child.try_wait().map_err(|err| err.to_string())? {
			return Err(format!("虚拟网卡没有启动（退出码 {status}）。请允许管理员权限。"));
		}
		if let Ok(nic) = Nic::connect(nic_port) {
			break nic;
		}
		if Instant::now() > deadline {
			let _ = child.kill();
			let _ = child.wait();
			return Err("等待虚拟网卡超时".into());
		}
		thread::sleep(Duration::from_millis(200));
	};

	let mut stop_listen = Arc::new(AtomicBool::new(false));
	let cancels: Arc<Mutex<std::collections::HashMap<String, Arc<AtomicBool>>>> = Arc::new(Mutex::new(std::collections::HashMap::new()));
	let ids = Arc::new(AtomicU64::new(1));
	let mut listener: Option<(JoinHandle<()>, String)> = None;
	let mut current_ip = String::new();
	let mut bind_message = String::new();
	let mut saw_ready = false;

	loop {
		while let Ok(cmd) = rx.try_recv() {
			match cmd {
				Cmd::Leave => {
					finish(app, child, &mut nic, stop_listen.as_ref(), cancels.as_ref(), listener.take(), download_dir.as_ref());
					return Ok(());
				}
				Cmd::SetDir { path } => {
					*download_dir.lock().expect("dir") = PathBuf::from(path);
				}
				Cmd::Cancel { id } => {
					if let Some(flag) = cancels.lock().expect("cancels").get(&id) {
						flag.store(true, Ordering::Relaxed);
					}
				}
				Cmd::Send { peer, paths } => {
					let app = app.clone();
					let cancels = Arc::clone(&cancels);
					let ids = Arc::clone(&ids);
					thread::spawn(move || send_paths(app, cancels, ids, peer, paths));
				}
			}
		}
		if let Some(status) = child.try_wait().map_err(|err| err.to_string())? {
			stop_listen.store(true, Ordering::Relaxed);
			return Err(format!("客户端已退出（{status}）"));
		}
		let virt = nic.virt().unwrap_or_else(|_| current_ip.clone());
		let tun = nic.tun().unwrap_or_default();
		let peers = nic.peers().unwrap_or_default();
		let beacons = nic.status().unwrap_or_default();
		let ready = !virt.is_empty() && virt != "0.0.0.0";
		if ready {
			saw_ready = true;
		} else if saw_ready {
			finish(app, child, &mut nic, stop_listen.as_ref(), cancels.as_ref(), listener.take(), download_dir.as_ref());
			return Err("房间已关闭或票据失效".into());
		}
		if ready && listener.as_ref().map(|(_, ip)| ip != &virt).unwrap_or(true) {
			stop_listen.store(true, Ordering::Relaxed);
			if let Some((handle, _)) = listener.take() {
				let _ = handle.join();
			}
			stop_listen = Arc::new(AtomicBool::new(false));
			match TcpListener::bind((virt.as_str(), PORT)) {
				Ok(socket) => {
					let _ = socket.set_nonblocking(true);
					let stop = Arc::clone(&stop_listen);
					let dir = Arc::clone(download_dir);
					let app_listen = app.clone();
					let cancels_listen = Arc::clone(&cancels);
					let ids_listen = Arc::clone(&ids);
					let handle = thread::spawn(move || listen_files(app_listen, socket, dir, stop, cancels_listen, ids_listen));
					listener = Some((handle, virt.clone()));
					bind_message.clear();
				}
				Err(err) => {
					bind_message = format!("文件端口没有绑上: {err}");
				}
			}
		}
		current_ip = virt.clone();
		let views = peers.iter().map(|peer| peer_view(peer, &virt, &beacons)).collect();
		let dir = download_dir.lock().expect("dir").display().to_string();
		let message = if !bind_message.is_empty() {
			bind_message.clone()
		} else if ready {
			String::new()
		} else {
			"正在等待房间分配地址".into()
		};
		emit_room(
			app,
			&RoomState {
				phase: if ready { "joined".into() } else { "joining".into() },
				virtual_ip: if ready { virt } else { String::new() },
				tun,
				message,
				download_dir: dir,
				peers: views,
			},
		);
		thread::sleep(Duration::from_millis(800));
	}
}

fn peer_view(peer: &Peer, mine: &str, beacons: &[Beacon]) -> PeerView {
	let beacon = beacons.iter().find(|item| item.virtual_ip == peer.virtual_ip);
	let (place, live) = match beacon {
		Some(item) if item.via == "local" => (item.place.clone(), "本机 · 状态经隧道发出".to_string()),
		Some(item) => {
			let path = if item.path == "direct" { "直连" } else { "中转" };
			let via = if item.via == "udp" { "隧道 UDP" } else { "隧道 TCP" };
			(item.place.clone(), format!("在线 · {path} · {via}"))
		}
		None => (String::new(), "等待隧道状态".to_string()),
	};
	PeerView {
		mac: peer.mac.clone(),
		source: format!("{}:{}", peer.source_ip, peer.source_port),
		virtual_ip: peer.virtual_ip.clone(),
		direct: peer.direct,
		myself: peer.virtual_ip == mine,
		place,
		live,
	}
}

fn finish(app: &AppHandle, child: &mut Child, nic: &mut Nic, stop: &AtomicBool, cancels: &Mutex<std::collections::HashMap<String, Arc<AtomicBool>>>, listener: Option<(JoinHandle<()>, String)>, download_dir: &Mutex<PathBuf>) {
	stop.store(true, Ordering::Relaxed);
	cancel_all(cancels);
	let _ = nic.quit();
	let deadline = Instant::now() + Duration::from_secs(5);
	loop {
		match child.try_wait() {
			Ok(Some(_)) => break,
			Ok(None) if Instant::now() > deadline => {
				let _ = child.kill();
				let _ = child.wait();
				break;
			}
			Ok(None) => thread::sleep(Duration::from_millis(100)),
			Err(_) => break,
		}
	}
	if let Some((handle, _)) = listener {
		let _ = handle.join();
	}
	let dir = download_dir.lock().expect("dir").display().to_string();
	emit_room(app, &idle_state(&dir));
}

fn cancel_all(cancels: &Mutex<std::collections::HashMap<String, Arc<AtomicBool>>>) {
	for flag in cancels.lock().expect("cancels").values() {
		flag.store(true, Ordering::Relaxed);
	}
}

fn next_id(ids: &AtomicU64) -> String {
	format!("{:x}", ids.fetch_add(1, Ordering::Relaxed))
}

fn send_paths(app: AppHandle, cancels: Arc<Mutex<std::collections::HashMap<String, Arc<AtomicBool>>>>, ids: Arc<AtomicU64>, peer: String, paths: Vec<String>) {
	for path in paths {
		let id = next_id(&ids);
		let cancel = Arc::new(AtomicBool::new(false));
		cancels.lock().expect("cancels").insert(id.clone(), Arc::clone(&cancel));
		let name = Path::new(&path).file_name().and_then(|text| text.to_str()).unwrap_or("file").to_string();
		let mut view = TransferView {
			id: id.clone(),
			direction: "send".into(),
			name: name.clone(),
			peer: peer.clone(),
			done: 0,
			total: 0,
			bytes_per_second: 0,
			state: "active".into(),
			message: String::new(),
		};
		let outcome = (|| {
			let mut file = File::open(&path).map_err(|err| err.to_string())?;
			let size = file.metadata().map_err(|err| err.to_string())?.len();
			view.total = size;
			let mut stream = TcpStream::connect((peer.as_str(), PORT)).map_err(|err| format!("连不上 {peer}: {err}"))?;
			let mut meter = Meter::new();
			transfer::send_file(&mut stream, &name, &mut file, size, &cancel, |done, total| {
				view.done = done;
				view.total = total;
				view.bytes_per_second = meter.update(done);
				emit_transfer(&app, &view);
			})
		})();
		view.state = match &outcome {
			Ok(()) => "done".into(),
			Err(err) if err == "已取消" => "cancelled".into(),
			Err(_) => "error".into(),
		};
		if let Err(err) = &outcome {
			view.message = err.clone();
		}
		emit_transfer(&app, &view);
		cancels.lock().expect("cancels").remove(&id);
	}
}

fn listen_files(app: AppHandle, listener: TcpListener, dir: Arc<Mutex<PathBuf>>, stop: Arc<AtomicBool>, cancels: Arc<Mutex<std::collections::HashMap<String, Arc<AtomicBool>>>>, ids: Arc<AtomicU64>) {
	while !stop.load(Ordering::Relaxed) {
		match listener.accept() {
			Ok((stream, addr)) => {
				let app = app.clone();
				let dir = Arc::clone(&dir);
				let cancels = Arc::clone(&cancels);
				let ids = Arc::clone(&ids);
				thread::spawn(move || receive_one(app, stream, addr.ip().to_string(), dir, cancels, ids));
			}
			Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => thread::sleep(Duration::from_millis(100)),
			Err(_) => thread::sleep(Duration::from_millis(200)),
		}
	}
}

fn receive_one(app: AppHandle, mut stream: TcpStream, peer: String, dir: Arc<Mutex<PathBuf>>, cancels: Arc<Mutex<std::collections::HashMap<String, Arc<AtomicBool>>>>, ids: Arc<AtomicU64>) {
	let id = next_id(&ids);
	let cancel = Arc::new(AtomicBool::new(false));
	cancels.lock().expect("cancels").insert(id.clone(), Arc::clone(&cancel));
	let view = TransferView {
		id: id.clone(),
		direction: "receive".into(),
		name: "接收中".into(),
		peer,
		done: 0,
		total: 0,
		bytes_per_second: 0,
		state: "active".into(),
		message: String::new(),
	};
	let mut meter = Meter::new();
	let folder = dir.lock().expect("dir").clone();
	let view = std::cell::RefCell::new(view);
	let outcome = transfer::receive_file(
		&mut stream,
		&folder,
		&cancel,
		|name, size| {
			let mut view = view.borrow_mut();
			view.name = name.to_string();
			view.total = size;
			emit_transfer(&app, &view);
		},
		|done, total| {
			let mut view = view.borrow_mut();
			view.done = done;
			view.total = total;
			view.bytes_per_second = meter.update(done);
			emit_transfer(&app, &view);
		},
	);
	let mut view = view.into_inner();
	view.state = match &outcome {
		Ok(_) => "done".into(),
		Err(err) if err == "已取消" => "cancelled".into(),
		Err(_) => "error".into(),
	};
	if let Err(err) = &outcome {
		view.message = err.clone();
	}
	emit_transfer(&app, &view);
	cancels.lock().expect("cancels").remove(&id);
}
