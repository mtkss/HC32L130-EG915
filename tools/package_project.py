"""生成可迁移源码包，排除本机依赖及编译中间文件，保留验证证据和固件。"""
from pathlib import Path
import zipfile
import argparse


def main():
    """仅打包当前工程目录，不跟随符号链接，不包含外部本机路径。"""
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="打包工程源码、文档及固件，排除本机依赖和Git内部对象。")
    parser.add_argument("--output-name", default=project_root.name + ".zip")
    archive_name = parser.parse_args().output_name
    if Path(archive_name).name != archive_name or not archive_name.lower().endswith(".zip"):
        raise SystemExit("output-name必须是当前工程上级目录内的ZIP文件名")
    archive_path = project_root.parent / archive_name
    excluded_suffixes = {".o", ".d", ".crf", ".dep", ".lnp", ".iex", ".pyc", ".uvoptx"}
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in sorted(project_root.rglob("*")):
            relative = item.relative_to(project_root)
            if item.is_symlink() or not item.is_file():
                continue
            if any(part in {".git", "test_runtime", "__pycache__"} for part in relative.parts):
                continue
            if item.suffix.lower() in excluded_suffixes or ".uvguix." in item.name:
                continue
            archive.write(item, project_root.name + "/" + relative.as_posix())
    with zipfile.ZipFile(archive_path) as archive:
        bad_entry = archive.testzip()
        if bad_entry:
            raise SystemExit("ZIP verification failed: " + bad_entry)
        print(f"Packaged {len(archive.infolist())} files: {archive_path}")


if __name__ == "__main__":
    main()
