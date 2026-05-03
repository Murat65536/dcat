#!/usr/bin/env python3

import pathlib
import sys


def main() -> int:
    src = pathlib.Path(sys.argv[1])
    dst = pathlib.Path(sys.argv[2])
    text = src.read_text(encoding="utf-8")
    text = text.replace("# define SIXELAPI __declspec(dllexport)", "# define SIXELAPI")
    dst.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
