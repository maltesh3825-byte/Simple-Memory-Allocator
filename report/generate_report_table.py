#!/usr/bin/env python3
"""
generate_report_table.py
Reads bench/results.csv and prints/updates the markdown table in report/REPORT.md.
Ensures that report numbers match the benchmark CSV byte-for-byte.
"""

import csv
import sys
import os

def generate_markdown_table(csv_path):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} does not exist", file=sys.stderr)
        sys.exit(1)

    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    header = "| Workload | Allocator | Total Ops | Time (ms) | Throughput (ops/sec) | Success Allocs | Failed Allocs | Frag Ratio | Largest Free Block |\n"
    separator = "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |\n"
    
    table_lines = [header, separator]
    for r in rows:
        frag = f"{float(r['Fragmentation_Ratio']):.4f}" if r['Fragmentation_Ratio'] != "N/A" else "N/A (unmeasured)"
        largest = f"{int(r['Largest_Free_Bytes']):,} B" if r['Largest_Free_Bytes'] != "N/A" else "N/A"
        time_ms = f"{float(r['Elapsed_ms']):.3f} ms"
        throughput = f"{float(r['Throughput_ops_sec']):,.1f} ops/s"
        
        line = f"| **{r['Workload']}** | {r['Allocator']} | {int(r['Ops']):,} | {time_ms} | {throughput} | {int(r['Success_Allocs']):,} | {int(r['Failed_Allocs']):,} | {frag} | {largest} |\n"
        table_lines.append(line)

    return "".join(table_lines)

def update_report_file(csv_path, report_path):
    table_md = generate_markdown_table(csv_path)
    
    if not os.path.exists(report_path):
        print(table_md)
        return

    with open(report_path, "r", encoding="utf-8") as f:
        content = f.read()

    start_marker = "<!-- BENCHMARK_TABLE_START -->"
    end_marker = "<!-- BENCHMARK_TABLE_END -->"

    if start_marker in content and end_marker in content:
        before = content.split(start_marker)[0]
        after = content.split(end_marker)[1]
        new_content = f"{before}{start_marker}\n\n{table_md}\n{end_marker}{after}"
        with open(report_path, "w", encoding="utf-8") as f:
            f.write(new_content)
        print(f"Successfully updated {report_path} with table from {csv_path}")
    else:
        print(table_md)

if __name__ == "__main__":
    csv_file = sys.argv[1] if len(sys.argv) > 1 else "bench/results.csv"
    report_file = sys.argv[2] if len(sys.argv) > 2 else "report/REPORT.md"
    update_report_file(csv_file, report_file)
