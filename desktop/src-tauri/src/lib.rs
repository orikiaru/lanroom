mod nic;
mod room;
mod transfer;

use room::Room;
use tauri::{Manager, State, WindowEvent};

struct AppState {
	room: Room,
}

#[tauri::command]
fn join_room(app: tauri::AppHandle, state: State<'_, AppState>, server: String, port: u16, room: String, bind: String, token: String) -> Result<(), String> {
	state.room.join(app, server, port, room, bind, token)
}

#[tauri::command]
fn leave_room(state: State<'_, AppState>) {
	state.room.leave();
}

#[tauri::command]
fn send_files(state: State<'_, AppState>, peer: String, paths: Vec<String>) -> Result<(), String> {
	state.room.send(peer, paths)
}

#[tauri::command]
fn cancel_transfer(state: State<'_, AppState>, id: String) -> Result<(), String> {
	state.room.cancel(id)
}

#[tauri::command]
fn set_download_dir(state: State<'_, AppState>, path: String) -> Result<(), String> {
	state.room.set_download_dir(path)
}

#[tauri::command]
fn download_dir(state: State<'_, AppState>) -> String {
	state.room.download_dir()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
	tauri::Builder::default()
		.runtime(tauri_runtime_wry::Wry::default())
		.plugin(tauri_plugin_dialog::init())
		.manage(AppState { room: Room::new() })
		.on_window_event(|window, event| {
			if matches!(event, WindowEvent::CloseRequested { .. } | WindowEvent::Destroyed) {
				window.state::<AppState>().room.leave();
			}
		})
		.invoke_handler(tauri::generate_handler![join_room, leave_room, send_files, cancel_transfer, set_download_dir, download_dir])
		.run(tauri::generate_context!())
		.expect("error while running tauri application");
}
