#!/usr/bin/env python3
"""Build the published 0.2.0 media code with this version's isolated fixture."""
import argparse
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--output',required=True,help='An empty output directory outside this checkout')
parser.add_argument('--cmake',default='cmake')
args=parser.parse_args()
repo=Path(__file__).resolve().parents[1]
output=Path(args.output).resolve()
if output==repo or repo in output.parents:
    raise SystemExit('Use a separate empty directory outside the checkout')
output.mkdir(parents=True,exist_ok=True)
if any(output.iterdir()):raise SystemExit('Output directory must be empty')
source=output/'app';source.mkdir()
reference='461e8022f507f8549cec082ba2b551e7608f34e5'
for name in ('peer.cpp','peer.h','encoder.cpp','encoder.h'):
    (source/name).write_bytes(subprocess.check_output(['git','show',reference+':app/'+name],cwd=repo))
(source/'benchmark.cpp').write_bytes((repo/'app/benchmark.cpp').read_bytes())
(output/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(Baseline LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0)
add_executable(media-benchmark app/benchmark.cpp app/peer.cpp app/encoder.cpp)
target_include_directories(media-benchmark BEFORE PRIVATE app)
target_compile_definitions(media-benchmark PRIVATE LAZARUS_BASELINE GST_USE_UNSTABLE_API)
target_link_libraries(media-benchmark PRIVATE Qt6::Widgets Qt6::Network PkgConfig::GST)
''')
subprocess.run([args.cmake,'-S',str(output),'-B',str(output/'build'),'-DCMAKE_BUILD_TYPE=Release'],check=True)
subprocess.run([args.cmake,'--build',str(output/'build'),'-j2'],check=True)
print(output/'build/media-benchmark')
