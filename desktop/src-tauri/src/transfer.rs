use std::fs::File;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

pub const PORT: u16 = 17666;
const MAGIC: &[u8; 4] = b"LRF1";

#[derive(Debug)]
pub enum HeaderError {
	Incomplete,
	Invalid,
}

pub fn sanitize_basename(name: &str) -> Option<String> {
	let name = name.trim();
	if name.is_empty() || name.contains('\0') {
		return None;
	}
	let base = Path::new(name).file_name()?.to_str()?;
	if base.is_empty() || base == "." || base == ".." {
		return None;
	}
	if base.contains('/') || base.contains('\\') {
		return None;
	}
	Some(base.to_string())
}

pub fn unique_path(dir: &Path, name: &str) -> PathBuf {
	let direct = dir.join(name);
	if !direct.exists() {
		return direct;
	}
	let path = Path::new(name);
	let stem = path.file_stem().and_then(|s| s.to_str()).unwrap_or(name);
	let ext = path.extension().and_then(|s| s.to_str());
	for index in 2..10_000 {
		let next = match ext {
			Some(ext) => format!("{stem} ({index}).{ext}"),
			None => format!("{stem} ({index})"),
		};
		let candidate = dir.join(next);
		if !candidate.exists() {
			return candidate;
		}
	}
	dir.join(format!("{stem}-{}", std::process::id()))
}

pub fn encode_header(name: &str, size: u64) -> Result<Vec<u8>, HeaderError> {
	let name = sanitize_basename(name).ok_or(HeaderError::Invalid)?;
	let bytes = name.as_bytes();
	if bytes.len() > u16::MAX as usize {
		return Err(HeaderError::Invalid);
	}
	let mut out = Vec::with_capacity(14 + bytes.len());
	out.extend_from_slice(MAGIC);
	out.extend_from_slice(&(bytes.len() as u16).to_be_bytes());
	out.extend_from_slice(bytes);
	out.extend_from_slice(&size.to_be_bytes());
	Ok(out)
}

pub fn decode_header(buf: &[u8]) -> Result<(String, u64, usize), HeaderError> {
	if buf.len() < 6 {
		return Err(HeaderError::Incomplete);
	}
	if &buf[0..4] != MAGIC {
		return Err(HeaderError::Invalid);
	}
	let name_len = u16::from_be_bytes([buf[4], buf[5]]) as usize;
	let total = 6 + name_len + 8;
	if buf.len() < total {
		return Err(HeaderError::Incomplete);
	}
	let name = std::str::from_utf8(&buf[6..6 + name_len]).map_err(|_| HeaderError::Invalid)?;
	let name = sanitize_basename(name).ok_or(HeaderError::Invalid)?;
	let size = u64::from_be_bytes(buf[6 + name_len..total].try_into().unwrap());
	Ok((name, size, total))
}

fn prepare(stream: &TcpStream) {
	let _ = stream.set_nodelay(true);
	let _ = stream.set_read_timeout(Some(Duration::from_millis(400)));
	let _ = stream.set_write_timeout(Some(Duration::from_millis(400)));
}

fn read_full(stream: &mut TcpStream, buf: &mut [u8], cancel: &AtomicBool) -> Result<(), String> {
	let mut got = 0;
	while got < buf.len() {
		if cancel.load(Ordering::Relaxed) {
			return Err("已取消".into());
		}
		match stream.read(&mut buf[got..]) {
			Ok(0) => return Err("连接已关闭".into()),
			Ok(n) => got += n,
			Err(err) if err.kind() == std::io::ErrorKind::WouldBlock || err.kind() == std::io::ErrorKind::TimedOut => continue,
			Err(err) if err.kind() == std::io::ErrorKind::Interrupted => continue,
			Err(err) => return Err(err.to_string()),
		}
	}
	Ok(())
}

fn write_full(stream: &mut TcpStream, mut buf: &[u8], cancel: &AtomicBool) -> Result<(), String> {
	while !buf.is_empty() {
		if cancel.load(Ordering::Relaxed) {
			return Err("已取消".into());
		}
		match stream.write(buf) {
			Ok(0) => return Err("连接已关闭".into()),
			Ok(n) => buf = &buf[n..],
			Err(err) if err.kind() == std::io::ErrorKind::WouldBlock || err.kind() == std::io::ErrorKind::TimedOut => continue,
			Err(err) if err.kind() == std::io::ErrorKind::Interrupted => continue,
			Err(err) => return Err(err.to_string()),
		}
	}
	Ok(())
}

pub fn send_file(stream: &mut TcpStream, name: &str, file: &mut File, size: u64, cancel: &AtomicBool, mut on_progress: impl FnMut(u64, u64)) -> Result<(), String> {
	prepare(stream);
	let header = encode_header(name, size).map_err(|_| "文件名无效".to_string())?;
	write_full(stream, &header, cancel)?;
	let mut answer = [0u8; 1];
	read_full(stream, &mut answer, cancel)?;
	if answer[0] != 1 {
		return Err("对方拒绝接收".into());
	}
	let mut sent = 0u64;
	let mut chunk = [0u8; 32 * 1024];
	on_progress(0, size);
	while sent < size {
		if cancel.load(Ordering::Relaxed) {
			return Err("已取消".into());
		}
		let want = std::cmp::min(chunk.len() as u64, size - sent) as usize;
		file.read_exact(&mut chunk[..want]).map_err(|err| err.to_string())?;
		write_full(stream, &chunk[..want], cancel)?;
		sent += want as u64;
		on_progress(sent, size);
	}
	let _ = stream.flush();
	Ok(())
}

pub fn receive_file(stream: &mut TcpStream, dir: &Path, cancel: &AtomicBool, mut on_begin: impl FnMut(&str, u64), mut on_progress: impl FnMut(u64, u64)) -> Result<PathBuf, String> {
	prepare(stream);
	let mut head = [0u8; 6];
	read_full(stream, &mut head, cancel)?;
	if &head[0..4] != MAGIC {
		let _ = stream.write_all(&[0]);
		return Err("不是房间文件".into());
	}
	let name_len = u16::from_be_bytes([head[4], head[5]]) as usize;
	let mut rest = vec![0u8; name_len + 8];
	read_full(stream, &mut rest, cancel)?;
	let mut full = Vec::with_capacity(6 + rest.len());
	full.extend_from_slice(&head);
	full.extend_from_slice(&rest);
	let (name, size, _) = decode_header(&full).map_err(|_| "文件头无效".to_string())?;
	std::fs::create_dir_all(dir).map_err(|err| err.to_string())?;
	let path = unique_path(dir, &name);
	let mut file = File::create(&path).map_err(|err| err.to_string())?;
	write_full(stream, &[1], cancel)?;
	on_begin(&name, size);
	let mut got = 0u64;
	let mut chunk = [0u8; 32 * 1024];
	on_progress(0, size);
	while got < size {
		if cancel.load(Ordering::Relaxed) {
			let _ = std::fs::remove_file(&path);
			return Err("已取消".into());
		}
		let want = std::cmp::min(chunk.len() as u64, size - got) as usize;
		read_full(stream, &mut chunk[..want], cancel).map_err(|err| {
			let _ = std::fs::remove_file(&path);
			err
		})?;
		file.write_all(&chunk[..want]).map_err(|err| err.to_string())?;
		got += want as u64;
		on_progress(got, size);
	}
	file.flush().map_err(|err| err.to_string())?;
	Ok(path)
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::io::Write;
	use std::net::TcpListener;
	use std::thread;

	#[test]
	fn header_roundtrip_and_rejects_paths() {
		let bytes = encode_header("notes.txt", 42).unwrap();
		let (name, size, len) = decode_header(&bytes).unwrap();
		assert_eq!(name, "notes.txt");
		assert_eq!(size, 42);
		assert_eq!(len, bytes.len());
		assert!(decode_header(&bytes[..bytes.len() - 1]).is_err());
		assert!(encode_header("../etc/passwd", 1).unwrap().windows(4).any(|w| w == b"pass"));
		let (safe, _, _) = decode_header(&encode_header("../etc/passwd", 1).unwrap()).unwrap();
		assert_eq!(safe, "passwd");
		assert!(sanitize_basename("..").is_none());
		assert!(sanitize_basename(".").is_none());
		assert!(sanitize_basename("").is_none());
		assert_eq!(sanitize_basename("dir/photo.png").as_deref(), Some("photo.png"));
	}

	#[test]
	fn collision_names_and_loopback_transfer() {
		let dir = std::env::temp_dir().join(format!("lanroom-transfer-{}", std::process::id()));
		let _ = std::fs::remove_dir_all(&dir);
		std::fs::create_dir_all(&dir).unwrap();
		std::fs::write(dir.join("note.txt"), b"old").unwrap();
		assert_eq!(unique_path(&dir, "note.txt"), dir.join("note (2).txt"));

		let listener = TcpListener::bind("127.0.0.1:0").unwrap();
		let port = listener.local_addr().unwrap().port();
		let dest = dir.clone();
		let server = thread::spawn(move || {
			let (mut stream, _) = listener.accept().unwrap();
			let cancel = AtomicBool::new(false);
			receive_file(&mut stream, &dest, &cancel, |_, _| {}, |_, _| {}).unwrap()
		});

		let source = dir.join("outgoing.bin");
		{
			let mut file = File::create(&source).unwrap();
			file.write_all(b"hello lanroom").unwrap();
		}
		let mut stream = TcpStream::connect(("127.0.0.1", port)).unwrap();
		let mut file = File::open(&source).unwrap();
		let cancel = AtomicBool::new(false);
		send_file(&mut stream, "note.txt", &mut file, 13, &cancel, |_, _| {}).unwrap();
		drop(stream);
		let saved = server.join().unwrap();
		assert_eq!(saved, dir.join("note (2).txt"));
		assert_eq!(std::fs::read(saved).unwrap(), b"hello lanroom");
		let _ = std::fs::remove_dir_all(&dir);
	}
}
