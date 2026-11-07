use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{self, Command, Stdio};

const PROJECT: &str = "minux";
const BUILD_DIR: &str = "build";
const LINKER_SCRIPT: &str = "linker.ld";
const KERNEL_BIN: &str = "kernel.elf";

struct Log {
    color: bool,
}

impl Log {
    fn new() -> Self {
        Log { color: env::var_os("NO_COLOR").is_none() }
    }
    fn paint(&self, code: &str, tag: &str) -> String {
        if self.color { format!("\x1b[{code}m{tag}\x1b[0m") } else { tag.to_string() }
    }
    fn info(&self, m: &str) { println!("{} {m}", self.paint("1;36", "[INFO]   ")); }
    fn cmd(&self, m: &str) { println!("{} {m}", self.paint("2", "[CMD]    ")); }
    fn ok(&self, m: &str) { println!("{} {m}", self.paint("1;32", "[SUCCESS]")); }
    fn error(&self, m: &str) { eprintln!("{} {m}", self.paint("1;31", "[ERROR]  ")); }
}

struct Options {
    run: bool,
    clean: bool,
    debug: bool,
}

fn parse_args() -> Result<Options, String> {
    let mut o = Options { run: true, clean: false, debug: false };
    for arg in env::args().skip(1) {
        match arg.as_str() {
            "--no-run" => o.run = false,
            "--clean" => o.clean = true,
            "--debug" => o.debug = true,
            "-h" | "--help" => {
                println!(
                    "Usage: cargo run -- [--no-run] [--clean] [--debug]\n\n\
                     \x20 --no-run   build only, do not launch QEMU\n\
                     \x20 --clean    remove build/ and rebuild from scratch\n\
                     \x20 --debug    launch QEMU with -s -S (waits for GDB on port 1234)\n\n\
                     Environment variables:\n\
                     \x20 CC         choose the C compiler (gcc / clang)\n\
                     \x20 NO_COLOR   disable colored output"
                );
                process::exit(0);
            }
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    Ok(o)
}

fn tool_exists(tool: &str) -> bool {
    Command::new(tool)
        .arg("--version")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn find_first(candidates: &[String]) -> Option<String> {
    candidates.iter().find(|t| tool_exists(t)).cloned()
}

fn run_step(log: &Log, mut cmd: Command) -> Result<(), String> {
    let prog = cmd.get_program().to_string_lossy().into_owned();
    let args: Vec<String> = cmd.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
    log.cmd(&format!("{prog} {}", args.join(" ")));

    let status = cmd.status().map_err(|e| format!("failed to run `{prog}`: {e}"))?;
    if status.success() { Ok(()) } else { Err(format!("`{prog}` failed ({status})")) }
}

fn is_stale(output: &Path, inputs: &[PathBuf]) -> bool {
    let out_time = match fs::metadata(output).and_then(|m| m.modified()) {
        Ok(t) => t,
        Err(_) => return true,
    };
    inputs.iter().any(|i| {
        fs::metadata(i).and_then(|m| m.modified()).map(|t| t > out_time).unwrap_or(true)
    })
}

fn files_with_ext(ext: &str) -> Result<Vec<PathBuf>, String> {
    let mut v: Vec<PathBuf> = fs::read_dir(".")
        .map_err(|e| format!("failed to read the project directory: {e}"))?
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.is_file() && p.extension().map_or(false, |x| x == ext))
        .collect();
    v.sort();
    Ok(v)
}

struct Toolchain {
    nasm: String,
    cc: String,
    ld: String,
    qemu: String,
}

fn check_tools(log: &Log, need_qemu: bool) -> Result<Toolchain, String> {
    log.info("Checking required tools...");

    let mut cc_candidates: Vec<String> = Vec::new();
    if let Ok(cc) = env::var("CC") {
        if !cc.is_empty() { cc_candidates.push(cc); }
    }
    cc_candidates.extend(["gcc", "clang", "cc"].map(String::from));

    let nasm = find_first(&["nasm".to_string()]);
    let cc = find_first(&cc_candidates);
    let ld = find_first(&["ld".to_string()]);
    let qemu = find_first(&["qemu-system-x86_64".to_string(), "qemu-system-i386".to_string()]);

    let show = |name: &str, f: &Option<String>| match f {
        Some(p) => log.info(&format!("  found   {name:<10} -> {p}")),
        None => log.error(&format!("  missing {name}")),
    };
    show("nasm", &nasm);
    show("gcc/clang", &cc);
    show("ld", &ld);
    if need_qemu { show("qemu", &qemu); }

    let mut missing = Vec::new();
    if nasm.is_none() { missing.push("nasm"); }
    if cc.is_none() { missing.push("gcc/clang"); }
    if ld.is_none() { missing.push("ld (binutils)"); }
    if need_qemu && qemu.is_none() { missing.push("qemu-system-x86_64"); }
    if !missing.is_empty() {
        return Err(format!(
            "missing tools: {}. Debian/Ubuntu: sudo apt install nasm gcc binutils qemu-system-x86",
            missing.join(", ")
        ));
    }

    Ok(Toolchain {
        nasm: nasm.unwrap(),
        cc: cc.unwrap(),
        ld: ld.unwrap(),
        qemu: qemu.unwrap_or_default(),
    })
}

fn prepare_build_dir(log: &Log, clean: bool) -> Result<PathBuf, String> {
    let dir = PathBuf::from(BUILD_DIR);
    if clean && dir.exists() {
        log.info(&format!("Cleaning `{BUILD_DIR}/`..."));
        fs::remove_dir_all(&dir).map_err(|e| format!("failed to remove {BUILD_DIR}/: {e}"))?;
    }
    fs::create_dir_all(&dir).map_err(|e| format!("failed to create {BUILD_DIR}/: {e}"))?;
    Ok(dir)
}

fn compile_asm_all(log: &Log, tc: &Toolchain, out_dir: &Path) -> Result<Vec<PathBuf>, String> {
    let sources = files_with_ext("asm")?;
    if sources.is_empty() {
        return Err("no .asm files found in the project root".into());
    }

    let mut objects = Vec::new();
    for src in sources {
        let stem = src.file_stem().unwrap().to_string_lossy().into_owned();
        let obj = out_dir.join(format!("{stem}.asm.o"));

        if is_stale(&obj, &[src.clone()]) {
            log.info(&format!("Assembling {} ...", src.display()));
            let mut cmd = Command::new(&tc.nasm);
            cmd.args(["-f", "elf32"]).arg(&src).arg("-o").arg(&obj);
            run_step(log, cmd)?;
        } else {
            log.info(&format!("{stem}.asm.o is up to date, skipping."));
        }
        objects.push(obj);
    }
    Ok(objects)
}

fn compile_c_all(log: &Log, tc: &Toolchain, out_dir: &Path) -> Result<Vec<PathBuf>, String> {
    let sources = files_with_ext("c")?;
    if sources.is_empty() {
        return Err("no .c files found in the project root".into());
    }
    let headers = files_with_ext("h")?;

    let mut objects = Vec::new();
    for src in sources {
        let stem = src.file_stem().unwrap().to_string_lossy().into_owned();
        let obj = out_dir.join(format!("{stem}.o"));

        let mut deps = headers.clone();
        deps.push(src.clone());

        if is_stale(&obj, &deps) {
            log.info(&format!("Compiling {} ...", src.display()));
            let mut cmd = Command::new(&tc.cc);
            cmd.args([
                "-m32", "-ffreestanding", "-fno-stack-protector", "-fno-pic", "-fno-pie",
                "-nostdlib", "-O2", "-Wall", "-Wextra", "-c",
            ])
            .arg(&src)
            .arg("-o")
            .arg(&obj);
            run_step(log, cmd)?;
        } else {
            log.info(&format!("{stem}.o is up to date, skipping."));
        }
        objects.push(obj);
    }
    Ok(objects)
}

fn link(log: &Log, tc: &Toolchain, out_dir: &Path, objects: &[PathBuf]) -> Result<PathBuf, String> {
    let out = out_dir.join(KERNEL_BIN);
    let mut deps = objects.to_vec();
    deps.push(PathBuf::from(LINKER_SCRIPT));
    if !is_stale(&out, &deps) {
        log.info(&format!("{KERNEL_BIN} is up to date, skipping link."));
        return Ok(out);
    }
    log.info("Linking kernel...");
    let mut cmd = Command::new(&tc.ld);
    cmd.args(["-m", "elf_i386", "-z", "noexecstack", "-T", LINKER_SCRIPT, "-o"])
        .arg(&out)
        .args(objects);
    run_step(log, cmd)?;
    Ok(out)
}

fn run_qemu(log: &Log, tc: &Toolchain, kernel: &Path, debug: bool) -> Result<(), String> {
    let mut cmd = Command::new(&tc.qemu);
    cmd.arg("-kernel").arg(kernel);
    if debug {
        log.info("Debug mode: QEMU is waiting for GDB on localhost:1234");
        log.info("In another terminal: gdb build/kernel.elf -ex 'target remote :1234'");
        cmd.args(["-s", "-S"]);
    } else {
        log.info("Launching QEMU (close the window or press Ctrl+C to exit)...");
    }
    run_step(log, cmd)
}

fn build_and_run(log: &Log, opts: &Options) -> Result<(), String> {
    log.info(&format!("=== {PROJECT}: build & run ==="));

    if !Path::new(LINKER_SCRIPT).is_file() {
        return Err(format!("`{LINKER_SCRIPT}` not found. Run `cargo run` from the {PROJECT} project root."));
    }

    let tc = check_tools(log, opts.run)?;
    let out_dir = prepare_build_dir(log, opts.clean)?;

    let mut objects = compile_asm_all(log, &tc, &out_dir)?;
    objects.extend(compile_c_all(log, &tc, &out_dir)?);
    let kernel = link(log, &tc, &out_dir, &objects)?;

    log.ok(&format!("Build complete! -> {}", kernel.display()));

    if opts.run {
        run_qemu(log, &tc, &kernel, opts.debug)?;
    }
    Ok(())
}

fn main() {
    let log = Log::new();
    let opts = match parse_args() {
        Ok(o) => o,
        Err(e) => {
            log.error(&e);
            process::exit(2);
        }
    };
    if let Err(e) = build_and_run(&log, &opts) {
        log.error(&e);
        log.error("Build failed.");
        process::exit(1);
    }
}
