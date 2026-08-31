fn main() {
    println!("cargo:rerun-if-env-changed=RUSTC_WORKSPACE_WRAPPER");

    let running_clippy = std::env::var_os("RUSTC_WORKSPACE_WRAPPER")
        .is_some_and(|wrapper| wrapper.to_string_lossy().contains("clippy-driver"));
    if std::env::var_os("CARGO_CFG_WINDOWS").is_some() && !running_clippy {
        embed_resource::compile_for(
            "resources/msi-gpu-mux.rc",
            ["msi-mux-switch"],
            embed_resource::NONE,
        )
        .manifest_required()
        .expect("failed to embed the required administrator manifest");
    }
}
