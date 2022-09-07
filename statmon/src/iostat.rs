use anyhow::Result;
use regex::RegexSet;
use std::fs::{File, OpenOptions};
use std::io::{prelude::*, SeekFrom};

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum IoTotals {
    ReadsCompleted,
    ReadsMerged,
    SectorsRead,
    MsReading,
    WritesCompleted,
    WritesMerged,
    SectorsWritten,
    MsWriting,
}

pub struct IoStat {
    fp: Option<File>,
    io_totals: [u64; 8],
    io_totals_last: [u64; 8],
    ignore_set: RegexSet,
}

impl IoStat {
    pub fn new() -> Self {
        let ignore_set = RegexSet::new(&[r"^loop", r"^sd[a-z]\d+", r"^nvme\d+n\d+p"]).unwrap();

        Self {
            fp: None,
            io_totals: [0; 8],
            io_totals_last: [0; 8],
            ignore_set,
        }
    }

    pub fn update(&mut self, rdbuf: &mut String) -> Result<()> {
        if self.fp.is_none() {
            self.fp = Some(OpenOptions::new().read(true).open("/proc/diskstats")?);
            self.update(rdbuf)?;
        }
        let fp = self.fp.as_mut().unwrap();
        self.io_totals_last = self.io_totals;
        rdbuf.clear();
        fp.read_to_string(rdbuf)?;
        fp.seek(SeekFrom::Start(0))?;
        let is_first = self.io_totals[0] == 0;
        for line in rdbuf.lines().map(str::trim).filter(|l| !l.is_empty()) {
            let mut splitter = line.split_ascii_whitespace();
            let _ = splitter.next(); // id 1
            let _ = splitter.next(); // id 2
            let devname = splitter.next().unwrap_or_default();
            if devname.is_empty() || self.ignore_set.is_match(devname) {
                continue;
            }
            if is_first {
                eprintln!("Detected disk: {}", devname);
            }
            for (val, field) in splitter.zip(self.io_totals.iter_mut()) {
                let val: u64 = val.parse()?;
                *field = val;
            }
        }
        Ok(())
    }

    pub fn io_totals_ms(&self) -> [u64; 8] {
        let mut r = [0u64; 8];
        for (i, ritem) in r.iter_mut().enumerate().take(self.io_totals.len()) {
            *ritem = self.io_totals[i] - self.io_totals_last[i];
        }
        r
    }
}

impl Default for IoStat {
    fn default() -> Self {
        Self::new()
    }
}

#[test]
pub fn io_ignore_set_test() {
    let io = IoStat::new();
    let ignore_set = &io.ignore_set;
    assert!(ignore_set.is_match("loop123"));
    assert!(ignore_set.is_match("sda1"));
    assert!(!ignore_set.is_match("sda"));
    assert!(ignore_set.is_match("nvme0n0p0"));
    assert!(!ignore_set.is_match("nvme0n0"));
    assert!(!ignore_set.is_match("md100"));
}
