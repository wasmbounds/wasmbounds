use anyhow::Result;
use argh::FromArgs;
use parking_lot::Mutex;
use std::{sync::Arc, time::Duration};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    time::MissedTickBehavior,
};

mod cpustat;
mod eventstat;
mod iostat;
mod stats;
use crate::stats::Stats;

#[derive(FromArgs, Debug, Clone)]
/// Monitoring resource usage over a TCP socket
struct Options {
    /// port to listen on
    #[argh(option, short = 'p', default = "8125")]
    port: u16,
    /// hostname prefix for stats
    #[argh(option, short = 'x', default = "String::from(\"local_\")")]
    host_prefix: String,
    /// network interface name to monitor
    #[argh(option, short = 'm')]
    netdev: String,
}

async fn conn_handler(mut conn: tokio::net::TcpStream, stats: Arc<Mutex<Stats>>) -> Result<()> {
    conn.set_nodelay(true)?;
    let (rc, mut wc) = conn.split();
    let mut rc = tokio::io::BufReader::new(rc);
    let mut rdbuf = [0u8];
    loop {
        let _bytes_read = rc.read_exact(&mut rdbuf).await?;
        match rdbuf[0] {
            b'\n' | b'\r' | b'\0' => continue,
            b'q' => break,
            b'[' => {
                let mut st = stats.lock();
                st.update()?;
                st.begin_range();
            }
            b']' => {
                let stats = {
                    let mut st = stats.lock();
                    st.update()?;
                    st.end_range();
                    st.stat_packet()
                };
                wc.write_all(stats.as_bytes()).await?;
            }
            b'?' => {
                let stats = stats.lock().stat_packet();
                wc.write_all(stats.as_bytes()).await?;
            }
            u => eprintln!("Warning: {}", anyhow::anyhow!("Unknown command byte {}", u)),
        }
    }
    wc.shutdown().await?;
    Ok(())
}

async fn async_main(opts: &'static Options) -> Result<()> {
    let mut stats = Mutex::new(Stats::new());
    {
        let mut stats = stats.get_mut();
        stats.host_prefix = opts.host_prefix.clone();
        stats.net_iface = opts.netdev.clone();
        stats.update()?;
        stats.update()?;
    }
    let sock = tokio::net::TcpListener::bind(std::net::SocketAddrV4::new(
        std::net::Ipv4Addr::new(0, 0, 0, 0),
        opts.port,
    ))
    .await?;
    let stats = Arc::new(stats);
    let int_stats = Arc::clone(&stats);
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_millis(500));
        interval.set_missed_tick_behavior(MissedTickBehavior::Skip);
        loop {
            interval.tick().await;
            if let Err(e) = int_stats.lock().update() {
                eprintln!("Couldn't update statistics: {}", e);
                panic!("{}", e);
            }
        }
    });
    eprintln!("[status] Listening on port {}", opts.port);
    loop {
        match sock.accept().await {
            Ok((conn, addr)) => {
                let conn_stats = Arc::clone(&stats);
                tokio::spawn(async move {
                    if let Err(e) = conn_handler(conn, conn_stats).await {
                        eprintln!("Error handling connection from {}: {}", addr, e);
                    }
                });
            }
            Err(e) => {
                eprintln!("Error accepting connection: {}", e)
            }
        }
    }
}

fn main() -> Result<()> {
    let opts: &'static Options = Box::leak(Box::new(argh::from_env()));
    eprintln!("[status] Parsed options: {:?}", opts);
    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_time()
        .enable_io()
        .build()?;
    rt.block_on(async_main(opts))?;
    rt.shutdown_timeout(Duration::from_secs(5));
    Ok(())
}
