// Kitty's shared memory transfer (t=s). Frames go into POSIX shared memory
// objects that the terminal reads directly, skipping compression, base64 and
// the terminal stream. It only works when the terminal runs on the same
// machine, so support is checked first.

use std::{
    collections::VecDeque,
    io::{self, Write},
    process,
    sync::atomic::{AtomicU64, Ordering},
    thread,
    time::{Duration, Instant},
};

use base64::{Engine, engine::general_purpose::STANDARD as BASE64};
use rustix::{
    event::{PollFd, PollFlags, Timespec},
    fd::OwnedFd,
    fs::Mode,
    shm,
};

// How long to wait for the terminal to answer the support query
const QUERY_TIMEOUT: Duration = Duration::from_secs(1);
// Frames written but not yet read, like the writer thread's one in progress
// and one queued. The terminal removes each object once it has read it.
const MAX_UNREAD: usize = 2;
// If the terminal reads nothing for this long, shared memory isn't working
const READ_TIMEOUT: Duration = Duration::from_secs(1);

// Numbers object names, so no two in this process collide
static NEXT_ID: AtomicU64 = AtomicU64::new(0);

// Shared memory objects holding frames for the terminal to read
pub struct SharedFrames {
    unread: VecDeque<String>,
    read_timeout: Duration,
    // Set once the terminal stops reading; frames then go inline instead
    failed: bool,
}

impl Default for SharedFrames {
    fn default() -> Self {
        Self {
            unread: VecDeque::new(),
            read_timeout: READ_TIMEOUT,
            failed: false,
        }
    }
}

impl SharedFrames {
    // Copy a frame into a new shared memory object and return its name
    pub fn write(&mut self, data: &[u8]) -> io::Result<String> {
        if self.failed {
            return Err(io::Error::other(
                "the terminal stopped reading shared memory",
            ));
        }
        self.wait_for_reads()?;

        let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
        let name = format!("/dcat-{}-{id}", process::id());
        write_object(&name, data)?;
        self.unread.push_back(name.clone());
        Ok(name)
    }

    // Only a tiny command goes through the terminal stream, so unlike inline
    // pixels it doesn't hold dcat back when the terminal falls behind. Wait
    // here for the terminal to read earlier frames instead.
    fn wait_for_reads(&mut self) -> io::Result<()> {
        let deadline = Instant::now() + self.read_timeout;
        loop {
            self.unread.retain(|name| exists(name));
            if self.unread.len() < MAX_UNREAD {
                return Ok(());
            }
            if Instant::now() >= deadline {
                self.failed = true;
                self.remove_unread();
                return Err(io::Error::other(
                    "the terminal stopped reading shared memory",
                ));
            }
            thread::sleep(Duration::from_millis(1));
        }
    }

    fn remove_unread(&mut self) {
        for name in self.unread.drain(..) {
            let _ = shm::unlink(&name);
        }
    }
}

impl Drop for SharedFrames {
    fn drop(&mut self) {
        self.remove_unread();
    }
}

// Whether an object is still there, so not yet read by the terminal
fn exists(name: &str) -> bool {
    shm::open(name, shm::OFlags::RDONLY, Mode::empty()).is_ok()
}

fn write_object(name: &str, data: &[u8]) -> io::Result<()> {
    let fd = shm::open(
        name,
        shm::OFlags::CREATE | shm::OFlags::EXCL | shm::OFlags::RDWR,
        Mode::RUSR | Mode::WUSR,
    )?;
    let result = fill(&fd, data);
    if result.is_err() {
        let _ = shm::unlink(name);
    }
    Ok(result?)
}

// Linux can write to shared memory directly. That fills the pages without a
// fault on each one, which takes around a third less time than mapping it.
#[cfg(target_os = "linux")]
fn fill(fd: &OwnedFd, data: &[u8]) -> rustix::io::Result<()> {
    let mut written = 0;
    while written < data.len() {
        written += rustix::io::write(fd, &data[written..])?;
    }
    Ok(())
}

// Elsewhere, macOS included, shared memory can only be written through a mapping
#[cfg(not(target_os = "linux"))]
fn fill(fd: &OwnedFd, data: &[u8]) -> rustix::io::Result<()> {
    use rustix::mm::{MapFlags, ProtFlags, mmap, munmap};
    use std::ptr;

    rustix::fs::ftruncate(fd, data.len() as u64)?;
    if data.is_empty() {
        return Ok(());
    }
    // SAFETY: a fresh shared mapping of exactly data.len() bytes, written
    // once and unmapped before anything else can use it
    unsafe {
        let map = mmap(
            ptr::null_mut(),
            data.len(),
            ProtFlags::READ | ProtFlags::WRITE,
            MapFlags::SHARED,
            fd,
            0,
        )?;
        ptr::copy_nonoverlapping(data.as_ptr(), map.cast::<u8>(), data.len());
        munmap(map, data.len())
    }
}

// Ask the terminal whether it can read frames from shared memory. Must run in
// raw mode, before anything else reads input.
pub fn terminal_supports() -> bool {
    let name = format!("/dcat-{}-probe", process::id());
    if write_object(&name, &[0; 3]).is_err() {
        return false;
    }

    // A device attributes query follows, which every terminal answers, so
    // there's no need to wait out the timeout when the first goes unanswered
    let query = format!(
        "\x1b_Gi=31,s=1,v=1,a=q,t=s,f=24;{}\x1b\\\x1b[c",
        BASE64.encode(&name)
    );
    let supported =
        send_and_read_reply(query.as_bytes()).is_ok_and(|reply| contains(&reply, b"\x1b_Gi=31;OK"));

    // The terminal removes objects it reads, but may not have read this one
    let _ = shm::unlink(&name);
    supported
}

fn send_and_read_reply(query: &[u8]) -> io::Result<Vec<u8>> {
    let mut stdout = io::stdout().lock();
    stdout.write_all(query)?;
    stdout.flush()?;

    let stdin = rustix::stdio::stdin();
    let deadline = Instant::now() + QUERY_TIMEOUT;
    let mut reply = Vec::new();
    while !device_attributes_received(&reply) {
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            break;
        }
        let timeout = Timespec::try_from(remaining).unwrap_or_default();
        let mut fds = [PollFd::new(&stdin, PollFlags::IN)];
        if rustix::event::poll(&mut fds, Some(&timeout))? == 0 {
            break;
        }
        let mut chunk = [0; 256];
        let read = rustix::io::read(stdin, &mut chunk)?;
        if read == 0 {
            break;
        }
        reply.extend_from_slice(&chunk[..read]);
    }
    Ok(reply)
}

// The answer to a device attributes query looks like ESC [ ? 62 ; 22 c
fn device_attributes_received(reply: &[u8]) -> bool {
    reply
        .windows(3)
        .position(|window| window == b"\x1b[?")
        .is_some_and(|start| reply[start..].contains(&b'c'))
}

fn contains(haystack: &[u8], needle: &[u8]) -> bool {
    haystack
        .windows(needle.len())
        .any(|window| window == needle)
}

#[cfg(test)]
mod tests {
    use super::*;
    use rustix::mm::{MapFlags, ProtFlags, mmap, munmap};
    use std::ptr;

    #[test]
    fn frames_are_written_and_cleaned_up() {
        let data: Vec<u8> = (0..=255).collect();
        let name = {
            let mut frames = SharedFrames::default();
            let name = frames.write(&data).unwrap();
            assert!(name.starts_with('/') && !name[1..].contains('/'));
            // macOS limits shared memory names to 31 characters
            assert!(name.len() <= 31, "{name}");

            // Read it back the way the terminal does
            let fd = shm::open(name.as_str(), shm::OFlags::RDONLY, Mode::empty()).unwrap();
            let read = unsafe {
                let map = mmap(
                    ptr::null_mut(),
                    data.len(),
                    ProtFlags::READ,
                    MapFlags::SHARED,
                    &fd,
                    0,
                )
                .unwrap();
                let copy = std::slice::from_raw_parts(map.cast::<u8>(), data.len()).to_vec();
                munmap(map, data.len()).unwrap();
                copy
            };
            assert_eq!(read, data);
            name
        };
        // Dropping removes objects the terminal hasn't read
        assert!(shm::open(name.as_str(), shm::OFlags::RDONLY, Mode::empty()).is_err());
    }

    // Stands in for the terminal: reads (removes) every object it's given
    fn read_all(names: &[String]) {
        for name in names {
            shm::unlink(name.as_str()).unwrap();
        }
    }

    #[test]
    fn waits_for_the_terminal_to_read() {
        let mut frames = SharedFrames::default();
        let first = frames.write(&[1, 2, 3]).unwrap();
        let second = frames.write(&[1, 2, 3]).unwrap();

        // A reader in another thread catches up after a short while; the
        // third write has to wait for it rather than run ahead
        let reader = thread::spawn(move || {
            thread::sleep(Duration::from_millis(50));
            read_all(&[first, second]);
        });
        let start = Instant::now();
        let third = frames.write(&[1, 2, 3]).unwrap();
        assert!(
            start.elapsed() >= Duration::from_millis(40),
            "{:?}",
            start.elapsed()
        );
        reader.join().unwrap();
        assert!(exists(&third));
    }

    #[test]
    fn gives_up_when_the_terminal_never_reads() {
        let mut frames = SharedFrames::default();
        frames.read_timeout = Duration::from_millis(20);
        let names: Vec<String> = (0..MAX_UNREAD)
            .map(|_| frames.write(&[1, 2, 3]).unwrap())
            .collect();

        assert!(frames.write(&[1, 2, 3]).is_err());
        // Its unread objects are removed, and it doesn't try again
        assert!(names.iter().all(|name| !exists(name)));
        assert!(frames.write(&[1, 2, 3]).is_err());
    }

    #[test]
    #[ignore = "timing only; run with --release --ignored"]
    fn write_time() {
        let data = vec![30u8; 2560 * 1420 * 4];
        let mut frames = SharedFrames::default();
        let start = Instant::now();
        for _ in 0..20 {
            frames.write(&data).unwrap();
        }
        eprintln!(
            "shared memory write: {:?} per 2560x1420 frame",
            start.elapsed() / 20
        );
    }

    #[test]
    fn device_attributes_reply_detection() {
        assert!(device_attributes_received(b"\x1b[?62;22c"));
        assert!(device_attributes_received(b"\x1b_Gi=31;OK\x1b\\\x1b[?1;2c"));
        assert!(!device_attributes_received(b"\x1b_Gi=31;OK\x1b\\"));
        assert!(!device_attributes_received(b"\x1b[?62;22"));
    }
}
