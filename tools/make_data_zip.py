#!/usr/bin/env python3
# Create a SparkFS-compatible ZIP of the PETSCII Robots runtime data for RISC OS.
# Uses gerph's rozipinfo/rozipfile (BSD-2-Clause, see LICENSE.gerph-python-zipinfo-riscos).
import os
import shutil
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rozipfile import RISCOSZipFile

ASSET_SRC = "../PETSCIIRobots-Amiga"

FILES = {
    "Amiga":
        [
            "C64Font.raw", "Tiles.raw", "AnimTiles.raw", "Faces.raw",
            "Items.raw", "Keys.raw", "Health.raw", "Sprites.raw",
            "SpritesMask.raw", "SquareWave.raw",
        ],
    "Amiga/Data":
        [
            "IntroScreen.raw.gz", "GameScreen.raw.gz", "GameOver.raw.gz",
        ],
}

GZIP_FILETYPE = 0xF89
DATA_FILETYPE = 0xFFD


def stage(src_root, dest_root):
    for directory, leaves in FILES.items():
        dest_dir = os.path.join(dest_root, directory)
        os.makedirs(dest_dir)
        for leaf in leaves:
            shutil.copyfile(os.path.join(src_root, directory, leaf),
                            os.path.join(dest_dir, leaf))
    os.makedirs(os.path.join(dest_root, "Music"), exist_ok=True)
    for leaf in sorted(os.listdir(os.path.join(src_root, "Music"))):
        shutil.copyfile(os.path.join(src_root, "Music", leaf),
                        os.path.join(dest_root, "Music", leaf))
    os.makedirs(os.path.join(dest_root, "Sounds"), exist_ok=True)
    for leaf in sorted(os.listdir(os.path.join(src_root, "Sounds"))):
        shutil.copyfile(os.path.join(src_root, "Sounds", leaf),
                        os.path.join(dest_root, "Sounds", leaf))
    shutil.copyfile(os.path.join(src_root, "tileset.amiga"),
                    os.path.join(dest_root, "tileset.amiga"))


def write_tree(rzh, filename):
    if os.path.isdir(filename):
        zipname = os.path.relpath(filename, rzh.base_dir)
        zi = rzh.cls_zipinfo.from_file(filename=filename, arcname=zipname, nfs_encoding=True)
        zi.nfs_encoding = False
        rzh.zh.writestr(zi, b"")
        for name in sorted(os.listdir(filename)):
            write_tree(rzh, os.path.join(filename, name))
    elif os.path.isfile(filename):
        zipname = os.path.relpath(filename, rzh.base_dir)
        zi = rzh.cls_zipinfo.from_file(filename=filename, arcname=zipname, nfs_encoding=True)
        zi.nfs_encoding = False
        if zipname.endswith(".gz"):
            zi.riscos_filetype = GZIP_FILETYPE
        else:
            zi.riscos_filetype = DATA_FILETYPE
        with open(filename, "rb") as fh:
            data = fh.read()
        rzh.zh.writestr(zi, data)
    else:
        raise RuntimeError("not a file or directory: %s" % filename)


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: %s <output.zip> [asset-src-dir]\n" % sys.argv[0])
        sys.exit(1)
    output = sys.argv[1]
    src_root = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else ASSET_SRC)
    staged = tempfile.mkdtemp(prefix="petrobots-data-")
    os.makedirs(os.path.join(staged, "Music"))
    os.makedirs(os.path.join(staged, "Sounds"))
    stage(src_root, staged)
    with RISCOSZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                       base_dir=staged, default_filetype=None) as rzh:
        for name in sorted(os.listdir(staged)):
            write_tree(rzh, os.path.join(staged, name))
    shutil.rmtree(staged)
    print("Created %s (SparkFS-compatible ZIP, filetype Zip &A91)" % output)


if __name__ == "__main__":
    main()