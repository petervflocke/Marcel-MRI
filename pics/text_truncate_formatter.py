#!/usr/bin/env python3
import argparse
import sys

def process_file(input_file: str, max_len: int, output_file: str = "out.txt"):
    lines_out = []
    with open(input_file, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            line = line.replace('"', "")     # remove quotes
            line = line[:max_len]              # truncate from right
            line = line.rstrip()               # remove trailing spaces only
            lines_out.append(line)

    with open(output_file, "w", encoding="utf-8") as out:
        out.write("constexpr const char* const kDiagnosticScrollLines[] = {\n")
        for l in lines_out:
            out.write(f"  \"{l}\",\n")
        out.write("  \"\",\n")
        out.write("};\n")


def main():
    parser = argparse.ArgumentParser(description="Truncate and format text file for C++ constexpr output")
    parser.add_argument("--l", type=int, required=True, help="maximum line length")
    parser.add_argument("file", help="input text file")

    args = parser.parse_args()

    if args.l < 0:
        print("Error: --l must be >= 0", file=sys.stderr)
        sys.exit(1)

    process_file(args.file, args.l)


if __name__ == "__main__":
    main()

