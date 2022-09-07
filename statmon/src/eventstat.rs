use crate::stats::{Stat, WhichStat};
use anyhow::{Context, Result};
use perfcnt::{linux::PerfCounterBuilderLinux, AbstractPerfCounter, PerfCounter};
use std::collections::HashMap;

pub struct EventStat {
    events: HashMap<WhichStat, Vec<PerfCounter>>,
}

impl EventStat {
    pub fn new() -> Self {
        use perfcnt::linux::{
            CacheId, CacheOpId, CacheOpResultId, HardwareEventType, SoftwareEventType,
        };
        use CacheId as CId;
        use CacheOpId as COp;
        use CacheOpResultId as CRes;
        use WhichStat::*;
        let hwe = PerfCounterBuilderLinux::from_hardware_event;
        let swe = PerfCounterBuilderLinux::from_software_event;
        let cce = PerfCounterBuilderLinux::from_cache_event;
        Self {
            events: HashMap::new(),
        }
        .with_event(EventCycles, /*            */ hwe(HardwareEventType::CPUCycles))
        .with_event(EventInstructions, /*      */ hwe(HardwareEventType::Instructions))
        .with_event(EventCacheAccesses, /*     */ hwe(HardwareEventType::CacheReferences))
        .with_event(EventCacheMisses, /*       */ hwe(HardwareEventType::CacheMisses))
        .with_event(EventBranches, /*          */ hwe(HardwareEventType::BranchInstructions))
        .with_event(EventBranchMisses, /*      */ hwe(HardwareEventType::BranchMisses))
        .with_event(EventStalledFrontCycles, hwe(HardwareEventType::StalledCyclesFrontend))
        .with_event(EventStalledBackCycles, /* */ hwe(HardwareEventType::StalledCyclesBackend))
        .with_event(EventRefCycles, /*         */ hwe(HardwareEventType::RefCPUCycles))
        .with_event(EventPageFaults, /*        */ swe(SoftwareEventType::PageFaults))
        .with_event(EventPageFaultsMinor, /*   */ swe(SoftwareEventType::PageFaultsMaj))
        .with_event(EventPageFaultsMajor, /*   */ swe(SoftwareEventType::PageFaultsMin))
        .with_event(EventCpuMigrations, /*     */ swe(SoftwareEventType::CpuMigrations))
        //
        .with_event(EventL1DAccesses, /*      */ cce(CId::L1D, COp::Read, CRes::Access))
        .with_event(EventL1DMisses, /*        */ cce(CId::L1D, COp::Read, CRes::Miss))
        //
        .with_event(EventL1IAccesses, /*      */ cce(CId::L1I, COp::Read, CRes::Access))
        .with_event(EventL1IMisses, /*        */ cce(CId::L1I, COp::Read, CRes::Miss))
        //
        .with_event(EventDTLBAccesses, /*     */ cce(CId::DTLB, COp::Read, CRes::Access))
        .with_event(EventDTLBMisses, /*       */ cce(CId::DTLB, COp::Read, CRes::Miss))
        //
        .with_event(EventITLBAccesses, /*     */ cce(CId::ITLB, COp::Read, CRes::Access))
        .with_event(EventITLBMisses, /*       */ cce(CId::ITLB, COp::Read, CRes::Miss))
    }

    fn with_event(mut self, stat: WhichStat, mut ev: PerfCounterBuilderLinux) -> Self {
        let mut pcs: Vec<PerfCounter> = Vec::with_capacity(32);
        ev.for_all_pids();
        for cpuid in 0.. {
            match ev.on_cpu(cpuid).finish() {
                Ok(pc) => {
                    pcs.push(pc);
                }
                Err(_e) if cpuid > 0 => {
                    break;
                }
                Err(e) => {
                    eprintln!("Warning: couldn't initialize perf counter {stat:?}: {e}");
                    break;
                }
            }
        }
        if !pcs.is_empty() {
            self.events.insert(stat, pcs);
        }
        self
    }

    pub fn start(&self) -> Result<()> {
        for (&stat, events) in self.events.iter() {
            for event in events.iter() {
                event.reset().context(stat)?;
                event.start().context(stat)?;
            }
        }
        Ok(())
    }

    pub fn stop(&mut self) -> Result<()> {
        for (&stat, events) in self.events.iter() {
            for event in events.iter() {
                event.stop().context(stat)?;
            }
        }
        Ok(())
    }

    pub fn update(&mut self, stats: &mut [Stat]) -> Result<()> {
        for (&stat, events) in self.events.iter_mut() {
            let mut val = 0u64;
            for event in events.iter_mut() {
                val += event.read().context(stat)?;
            }
            stats[stat as usize].value_now.seti(val as i64);
        }
        Ok(())
    }
}

impl Default for EventStat {
    fn default() -> Self {
        Self::new()
    }
}
