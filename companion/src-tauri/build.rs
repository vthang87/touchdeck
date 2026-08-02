fn main() {
  #[cfg(target_os = "macos")]
  {
    // Embed Info.plist into the binary so `tauri dev` (non-.app) still gets
    // Bluetooth / TCC usage strings and a stable bundle id.
    let manifest_dir = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let plist = manifest_dir.join("Info.plist");
    if plist.exists() {
      println!("cargo:rerun-if-changed=Info.plist");
      println!(
        "cargo:rustc-link-arg=-Wl,-sectcreate,__TEXT,__info_plist,{}",
        plist.display()
      );
    }
  }
  tauri_build::build()
}
