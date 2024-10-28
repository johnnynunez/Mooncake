use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rustc-link-search=native=../../../build/mooncake-transfer-engine/src");
    println!("cargo:rustc-link-lib=static=transfer_engine");

    println!("cargo:rustc-link-lib=stdc++");
    println!("cargo:rustc-link-lib=ibverbs");
    println!("cargo:rustc-link-lib=glog");
    println!("cargo:rustc-link-lib=gflags");
    println!("cargo:rustc-link-lib=pthread");
    println!("cargo:rustc-link-lib=jsoncpp");
    println!("cargo:rustc-link-lib=numa");
    println!("cargo:rustc-link-lib=etcd-cpp-api");

    let bindings = bindgen::builder()
        .header("../../include/transfer_engine_c.h")
        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
