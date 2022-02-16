use std::collections::HashMap;
use std::env;
use std::fs::File;
use std::process::Command;
use std::path::Path;

fn main() {
    let qt_include_path = env::var("DEP_QT_INCLUDE_PATH").unwrap();
    let qt_library_path = env::var("DEP_QT_LIBRARY_PATH").unwrap();
    let qt_version      = env::var("DEP_QT_VERSION").unwrap();

    #[allow(unused_mut)]
    let mut config = cpp_build::Config::new();

    if cfg!(target_os = "macos") {
        config.flag("-F");
        config.flag(&qt_library_path);
    }

    let mut public_include = |name| {
        if cfg!(target_os = "macos") {
            config.include(format!("{}/{}.framework/Headers/", qt_library_path, name));
        }
        config.include(format!("{}/{}", qt_include_path, name));
    };
    public_include("QtCore");
    public_include("QtGui");
    public_include("QtQuick");
    public_include("QtQml");

    let mut private_include = |name| {
        if cfg!(target_os = "macos") {
            config.include(format!("{}/{}.framework/Headers/{}",       qt_library_path, name, qt_version));
            config.include(format!("{}/{}.framework/Headers/{}/{}",    qt_library_path, name, qt_version, name));
        }
        config.include(format!("{}/{}/{}",    qt_include_path, name, qt_version))
              .include(format!("{}/{}/{}/{}", qt_include_path, name, qt_version, name));
    };
    private_include("QtCore");
    private_include("QtGui");
    private_include("QtQuick");
    private_include("QtQml");

    let sdk: HashMap<&str, (&str, &str, &str, &str)> = vec![
        ("windows",  ("https://master.dl.sourceforge.net/project/mdk-sdk/nightly/mdk-sdk-windows-desktop-vs2022.7z?viasf=1", "lib/x64/",           "mdk.lib",    "include/")),
        ("linux",    ("https://master.dl.sourceforge.net/project/mdk-sdk/mdk-sdk-linux.tar.xz?viasf=1",              "lib/amd64/",         "libmdk.so",  "include/")),
        ("macos",    ("https://master.dl.sourceforge.net/project/mdk-sdk/mdk-sdk-macOS.tar.xz?viasf=1",              "lib/mdk.framework/", "mdk",        "include/")),
        ("android",  ("https://master.dl.sourceforge.net/project/mdk-sdk/mdk-sdk-android.7z?viasf=1",                "lib/arm64-v8a/",     "libmdk.so",  "include/")),
        ("ios",      ("https://master.dl.sourceforge.net/project/mdk-sdk/mdk-sdk-iOS.tar.xz?viasf=1",                "lib/mdk.framework/", "mdk",        "include/")),
    ].into_iter().collect();

    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    let entry = sdk[target_os.as_str()];

    if let Ok(path) = download_and_extract(&entry.0, &format!("{}{}", entry.1, entry.2)) {
        if target_os == "macos" || target_os == "ios" {
            println!("cargo:rustc-link-search=framework={}{}", path, "lib/");
            println!("cargo:rustc-link-lib=framework=mdk");
            config.flag_if_supported("-fobjc-arc");
            config.flag("-x").flag("objective-c++");
            let _ = Command::new("mkdir").args(&[&format!("{}/../../../../Frameworks", env::var("OUT_DIR").unwrap())]).status();
            Command::new("cp").args(&["-af", &format!("{}/lib/mdk.framework", path), &format!("{}/../../../../Frameworks/", env::var("OUT_DIR").unwrap())]).status().unwrap();
        } else {
            println!("cargo:rustc-link-search={}{}", path, entry.1);
            println!("cargo:rustc-link-lib=mdk");
        }
        if target_os == "windows" {
            std::fs::copy(format!("{}/bin/x64/mdk.dll", path), format!("{}/../../../mdk.dll", env::var("OUT_DIR").unwrap())).unwrap();
            std::fs::copy(format!("{}/bin/x64/ffmpeg-5.dll", path), format!("{}/../../../ffmpeg-5.dll", env::var("OUT_DIR").unwrap())).unwrap();
        }
        if target_os == "android" {
            std::fs::copy(format!("{}/lib/arm64-v8a/libffmpeg.so", path), format!("{}/../../../libffmpeg.so", env::var("OUT_DIR").unwrap())).unwrap();
            std::fs::copy(format!("{}/lib/arm64-v8a/libqtav-mediacodec.so", path), format!("{}/../../../libqtav-mediacodec.so", env::var("OUT_DIR").unwrap())).unwrap();
        }
        if target_os == "linux" {
            std::fs::copy(format!("{}/lib/amd64/libffmpeg.so.5", path), format!("{}/../../../libffmpeg.so.5", env::var("OUT_DIR").unwrap())).unwrap();
            std::fs::copy(format!("{}/lib/amd64/libmdk.so.0", path), format!("{}/../../../libmdk.so.0", env::var("OUT_DIR").unwrap())).unwrap();
        }
        config.include(format!("{}{}", path, entry.3));
    } else {
        panic!("Unable to download or extract mdk-sdk. Please make sure you have 7z in PATH or download mdk manually from https://sourceforge.net/projects/mdk-sdk/ and extract to {}", env::var("OUT_DIR").unwrap());
    }
    if target_os == "android" {
        config.flag("-std=c++17");
    }
    config
        .include(&qt_include_path)
        .flag_if_supported("-std=c++17")
        .flag_if_supported("/std:c++17")
        .flag_if_supported("/Zc:__cplusplus")
        .build("src/lib.rs");

    if cfg!(target_os = "macos") {
        config.flag("-F");
        config.flag(&qt_library_path);
    }
}

fn download_and_extract(url: &str, check: &str) -> Result<String, std::io::Error> {
    let out_dir = env::var("OUT_DIR").unwrap();
    if !Path::new(&format!("{}/mdk-sdk/{}", out_dir, check)).exists() {
        let ext = if url.contains(".tar.xz") { ".tar.xz" } else { ".7z" };
        {
            let mut reader = ureq::get(url).call().map_err(|_| std::io::ErrorKind::Other)?.into_reader();
            let mut file = File::create(format!("{}/mdk-sdk{}", out_dir, ext))?;
            std::io::copy(&mut reader, &mut file)?;
        }
        Command::new("7z").current_dir(&out_dir).args(&["x", "-y", &format!("mdk-sdk{}", ext)]).status()?;
        std::fs::remove_file(format!("{}/mdk-sdk{}", out_dir, ext))?;
        if ext == ".tar.xz" {
            let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
            if target_os == "macos" || target_os == "ios" || target_os == "linux" {
                Command::new("tar").current_dir(&out_dir).args(&["-xf", "mdk-sdk.tar"]).status()?;
            } else {
                Command::new("7z").current_dir(&out_dir).args(&["x", "-y", "mdk-sdk.tar"]).status()?;
            }
            std::fs::remove_file(format!("{}/mdk-sdk.tar", out_dir))?;
        }
    }

    Ok(format!("{}/mdk-sdk/", out_dir))
}
