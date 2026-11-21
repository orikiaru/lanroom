use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::time::Duration;

#[derive(Clone, Debug)]
pub struct Peer {
	pub mac: String,
	pub source_ip: String,
	pub source_port: u16,
	pub virtual_ip: String,
	pub direct: bool,
}

#[derive(Clone, Debug)]
#[allow(dead_code)]
pub struct Beacon {
	pub virtual_ip: String,
	pub mac: String,
	pub place: String,
	pub path: String,
	pub via: String,
	pub age_secs: u64,
}

pub struct Nic {
	reader: BufReader<TcpStream>,
}

impl Nic {
	pub fn connect(port: u16) -> std::io::Result<Self> {
		let stream = TcpStream::connect(("127.0.0.1", port))?;
		stream.set_read_timeout(Some(Duration::from_secs(3)))?;
		stream.set_write_timeout(Some(Duration::from_secs(3)))?;
		Ok(Self { reader: BufReader::new(stream) })
	}

	fn ask(&mut self, line: &str) -> std::io::Result<String> {
		let stream = self.reader.get_mut();
		stream.write_all(line.as_bytes())?;
		stream.write_all(b"\n")?;
		stream.flush()?;
		let mut reply = String::new();
		self.reader.read_line(&mut reply)?;
		if reply.is_empty() {
			return Err(std::io::Error::new(std::io::ErrorKind::UnexpectedEof, "empty reply"));
		}
		Ok(reply.trim_end().to_string())
	}

	pub fn virt(&mut self) -> std::io::Result<String> {
		let line = self.ask("VIRT")?;
		Ok(line.strip_prefix("virt ").unwrap_or("0.0.0.0").to_string())
	}

	pub fn tun(&mut self) -> std::io::Result<String> {
		let line = self.ask("TUN")?;
		Ok(line.strip_prefix("tun ").unwrap_or("").to_string())
	}

	pub fn peers(&mut self) -> std::io::Result<Vec<Peer>> {
		let line = self.ask("PEERS")?;
		let count: usize = line.strip_prefix("peers ").and_then(|text| text.parse().ok()).unwrap_or(0);
		let mut peers = Vec::with_capacity(count);
		for _ in 0..count {
			let row = self.line()?;
			let mut parts = row.split_whitespace();
			if parts.next() != Some("peer") {
				continue;
			}
			let Some(mac) = parts.next() else { continue };
			let Some(source_ip) = parts.next() else { continue };
			let Some(source_port) = parts.next().and_then(|text| text.parse().ok()) else { continue };
			let Some(virtual_ip) = parts.next() else { continue };
			let direct = parts.next() == Some("1");
			peers.push(Peer {
				mac: mac.to_string(),
				source_ip: source_ip.to_string(),
				source_port,
				virtual_ip: virtual_ip.to_string(),
				direct,
			});
		}
		Ok(peers)
	}

	pub fn status(&mut self) -> std::io::Result<Vec<Beacon>> {
		let line = self.ask("STATUS")?;
		let count: usize = line.strip_prefix("status ").and_then(|text| text.parse().ok()).unwrap_or(0);
		let mut rows = Vec::with_capacity(count);
		for _ in 0..count {
			let row = self.line()?;
			let mut parts = row.split_whitespace();
			if parts.next() != Some("client") {
				continue;
			}
			let Some(virtual_ip) = parts.next() else { continue };
			let Some(mac) = parts.next() else { continue };
			let Some(place) = parts.next() else { continue };
			let Some(path) = parts.next() else { continue };
			let Some(via) = parts.next() else { continue };
			let age_secs = parts.next().and_then(|text| text.parse().ok()).unwrap_or(0);
			rows.push(Beacon {
				virtual_ip: virtual_ip.to_string(),
				mac: mac.to_string(),
				place: place.to_string(),
				path: path.to_string(),
				via: via.to_string(),
				age_secs,
			});
		}
		Ok(rows)
	}

	pub fn quit(&mut self) -> std::io::Result<()> {
		let _ = self.ask("QUIT")?;
		Ok(())
	}

	fn line(&mut self) -> std::io::Result<String> {
		let mut reply = String::new();
		self.reader.read_line(&mut reply)?;
		Ok(reply.trim_end().to_string())
	}
}
