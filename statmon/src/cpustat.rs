use anyhow::Result;
use std::fs::{File, OpenOptions};
use std::io::{prelude::*, SeekFrom};

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum CpuTotals {
    UserMode,
    NiceUserMode,
    SystemMode,
    Idle,
    IoWait,
    Irq,
    SoftIrq,
    _Steal,
    _Guest,
    _GuestNice,
}

pub struct CpuStat {
    fp: Option<File>,
    /// milliseconds per clock tick
    ms_per_clk: u64,
    cpu_totals: [u64; 10],
    cpu_totals_last: [u64; 10],
    context_switches: u64,
    context_switches_last: u64,
    procs_running: u64,
    procs_io_blocked: u64,
}

impl CpuStat {
    pub const fn new() -> Self {
        Self {
            fp: None,
            ms_per_clk: 0,
            cpu_totals: [0; 10],
            cpu_totals_last: [0; 10],
            context_switches: 0,
            context_switches_last: 0,
            procs_running: 0,
            procs_io_blocked: 0,
        }
    }

    pub fn update(&mut self, rdbuf: &mut String) -> Result<()> {
        if self.fp.is_none() {
            self.fp = Some(OpenOptions::new().read(true).open("/proc/stat")?);
            self.ms_per_clk = 1000 / unsafe { libc::sysconf(libc::_SC_CLK_TCK) } as u64;
            self.update(rdbuf)?;
        }
        let fp = self.fp.as_mut().unwrap();
        self.cpu_totals_last = self.cpu_totals;
        self.context_switches_last = self.context_switches;
        rdbuf.clear();
        fp.read_to_string(rdbuf)?;
        fp.seek(SeekFrom::Start(0))?;
        for line in rdbuf.lines().map(str::trim).filter(|l| !l.is_empty()) {
            if line.starts_with("cpu ") {
                for (val, field) in line
                    .split_whitespace()
                    .skip(1)
                    .zip(self.cpu_totals.iter_mut())
                {
                    let val: u64 = val.parse()?;
                    *field = val * self.ms_per_clk;
                }
            } else if let Some(valstr) = line.strip_prefix("ctxt ") {
                let val: u64 = valstr.trim().parse()?;
                self.context_switches = val;
            } else if let Some(valstr) = line.strip_prefix("procs_running ") {
                let val: u64 = valstr.trim().parse()?;
                self.procs_running = val;
            } else if let Some(valstr) = line.strip_prefix("procs_blocked ") {
                let val: u64 = valstr.trim().parse()?;
                self.procs_io_blocked = val;
            }
        }
        Ok(())
    }

    pub fn cpu_totals_ms(&self) -> [u64; 10] {
        let mut r = [0u64; 10];
        for (i, ritem) in r.iter_mut().enumerate().take(self.cpu_totals.len()) {
            *ritem = self.cpu_totals[i] - self.cpu_totals_last[i];
        }
        r
    }

    pub fn context_switches(&self) -> u64 {
        self.context_switches - self.context_switches_last
    }

    pub fn procs_running(&self) -> u64 {
        self.procs_running
    }

    pub fn procs_io_blocked(&self) -> u64 {
        self.procs_io_blocked
    }
}

impl Default for CpuStat {
    fn default() -> Self {
        Self::new()
    }
}
