use std::collections::HashMap;
use std::env;
use std::fs::File;
use std::process::Command;
use std::path::Path;

fn main() {
    let qt_include_path = env::var("DEP_QT_INCLUDE_PATH").unwrap();
    let qt_library_path = env::var("DEP_QT_LIBRARY_PATH").unwrap();
    let qt_version      = env::var("DEP_QT_VERSION").unwrap();

    let mut config = cpp_build::Config::new();

    for f in std::env::var("DEP_QT_COMPILE_FLAGS").unwrap().split_terminator(";") {
        config.flag(f);
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

    #[cfg(feature = "mdk-nightly")]
    let nightly = "nightly/";
    #[cfg(not(feature = "mdk-nightly"))]
    let nightly = "";

    let sdk: HashMap<&str, (String, &str, &str, &str)> = vec![
        ("windows",  (format!("https://master.dl.sourceforge.net/project/mdk-sdk/{}mdk-sdk-windows-desktop-clang.7z?viasf=1", nightly),  "lib/x64/",           "mdk.lib",    "include/")),
        ("linux",    (format!("https://master.dl.sourceforge.net/project/mdk-sdk/{}mdk-sdk-linux.tar.xz?viasf=1", nightly),              "lib/amd64/",         "libmdk.so",  "include/")),
        ("macos",    (format!("https://master.dl.sourceforge.net/project/mdk-sdk/{}mdk-sdk-macOS.tar.xz?viasf=1", nightly),              "lib/mdk.framework/", "mdk",        "include/")),
        ("android",  (format!("https://master.dl.sourceforge.net/project/mdk-sdk/{}mdk-sdk-android.7z?viasf=1", nightly),                "lib/arm64-v8a/",     "libmdk.so",  "include/")),
        ("ios",      (format!("https://master.dl.sourceforge.net/project/mdk-sdk/{}mdk-sdk-iOS.tar.xz?viasf=1", nightly),                "lib/mdk.framework/", "mdk",        "include/")),
    ].into_iter().collect();

    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    let entry = &sdk[target_os.as_str()];

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
            println!("cargo:rustc-link-lib=dxguid");
            std::fs::copy(format!("{}/bin/x64/mdk.dll", path), format!("{}/../../../mdk.dll", env::var("OUT_DIR").unwrap())).unwrap();
            let _ = std::fs::copy(format!("{}/bin/x64/ffmpeg-5.dll", path), format!("{}/../../../ffmpeg-5.dll", env::var("OUT_DIR").unwrap()));
            let _ = std::fs::copy(format!("{}/bin/x64/mdk-braw.dll", path), format!("{}/../../../mdk-braw.dll", env::var("OUT_DIR").unwrap()));
            let _ = std::fs::copy(format!("{}/bin/x64/mdk-r3d.dll", path), format!("{}/../../../mdk-r3d.dll", env::var("OUT_DIR").unwrap()));

            let _ = std::fs::copy(format!("{}/bin/x64/mdk.pdb", path), format!("{}/../../../mdk.pdb", env::var("OUT_DIR").unwrap())).unwrap();
            let _ = std::fs::copy(format!("{}/bin/x64/mdk-braw.pdb", path), format!("{}/../../../mdk-braw.pdb", env::var("OUT_DIR").unwrap()));
            let _ = std::fs::copy(format!("{}/bin/x64/mdk-r3d.pdb", path), format!("{}/../../../mdk-r3d.pdb", env::var("OUT_DIR").unwrap()));
        }
        if target_os == "android" {
            std::fs::copy(format!("{}/lib/arm64-v8a/libmdk.so", path), format!("{}/../../../libmdk.so", env::var("OUT_DIR").unwrap())).unwrap();
            let _ = std::fs::copy(format!("{}/lib/arm64-v8a/libffmpeg.so", path), format!("{}/../../../libffmpeg.so", env::var("OUT_DIR").unwrap()));
            // std::fs::copy(format!("{}/lib/arm64-v8a/libqtav-mediacodec.so", path), format!("{}/../../../libqtav-mediacodec.so", env::var("OUT_DIR").unwrap())).unwrap();
        }
        if target_os == "linux" {
            let _ = std::fs::copy(format!("{}/lib/amd64/libffmpeg.so.5", path), format!("{}/../../../libffmpeg.so.5", env::var("OUT_DIR").unwrap()));
            std::fs::copy(format!("{}/lib/amd64/libmdk.so.0", path), format!("{}/../../../libmdk.so.0", env::var("OUT_DIR").unwrap())).unwrap();
            let _ = std::fs::copy(format!("{}/lib/amd64/libmdk-braw.so", path), format!("{}/../../../libmdk-braw.so", env::var("OUT_DIR").unwrap()));
            let _ = std::fs::copy(format!("{}/lib/amd64/libmdk-r3d.so", path), format!("{}/../../../libmdk-r3d.so", env::var("OUT_DIR").unwrap()));
        }
        config.include(format!("{}{}", path, entry.3));
    } else {
        panic!("Unable to download or extract mdk-sdk. Please make sure you have 7z in PATH or download mdk manually from https://sourceforge.net/projects/mdk-sdk/ and extract to {}", env::var("OUT_DIR").unwrap());
    }

    let vulkan_sdk = env::var("VULKAN_SDK");
    if let Ok(sdk) = vulkan_sdk {
        if !sdk.is_empty() {
            config.include(format!("{}/Include", sdk));
            config.include(format!("{}/include", sdk));
        }
    }

    config
        .include(&qt_include_path)
        .build("src/lib.rs");

}

fn download_and_extract(url: &str, check: &str) -> Result<String, std::io::Error> {
    if let Ok(path) = env::var("MDK_SDK") {
        if Path::new(&format!("{}/{}", path, check)).exists() {
            return Ok(path);
        }
    }
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
