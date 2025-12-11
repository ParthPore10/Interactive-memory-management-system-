# Interactive Memory Management System (IMMS)

Small C++ console simulator that visualizes paging and heap allocation strategies. It lets you allocate/deallocate memory blocks with first/best/worst fit, map process pages to frames, enqueue requests, and view fragmentation plus basic utilization stats in real time.

## Build
- Requires a C++17 compiler (uses `unistd.h` for delays).
- From the repo root: `g++ -std=c++17 main.cpp -o imms`

## Run
- Execute `./imms` (or `imms.exe` on Windows; remove `unistd.h`/`usleep` and swap for `Sleep` if needed).
- The program redraws the dashboard each tick; ANSI colors are used for clarity.

## What You See
- **Physical Memory Bar**: 20 frames (size 5 each) showing which PID owns a frame.
- **Page Table**: Fixed table for Process 1 (6 entries) mapping pages to frames.
- **Stats Panel**: Used/free frames, page count, and a simple heatmap of utilization.
- **Request Queue**: Pending allocation/deallocation requests with FIFO processing.
- **Fragmentation Analysis**: Total free, block counts, largest block, and ratio.

## Menu Actions
- `1` Immediate allocate: choose size and strategy (1=First, 2=Best, 3=Worst).
- `2` Immediate deallocate by PID.
- `3` Allocate N pages for Process 1 (one frame per page).
- `4` Fragmentation analysis.
- `5` Enqueue allocation request.
- `6` Enqueue deallocation request.
- `7` Process next queued request.
- `8` Run all queued requests automatically (with short delays for visuals).
- `q` Quit.

## Implementation Notes
- Memory is a linked list of `memory_block` nodes; splits/merges occur on alloc/free.
- Heaps are rebuilt on each request to pick best/worst free blocks quickly.
- Frames are derived from `start` offsets divided by `FRAME_SIZE`; constants live at the top of `main.cpp` for quick tuning.

## Test Cases
| Test Case | Expected Outcome |
| --- | --- |
| Allocate 10 units using First Fit | _first adequate block is selected_ |
| Allocate using Best Fit on multiple free holes | _smallest adequate block is selected_ |
| Allocate pages for process1 | _Frame allocation and page table update_ |
| Request size larger than any free block | _display no suitable block_ |
| Page Table Overflow | _display page table full_ |
| Deallocate invalid/terminated process | _display invalid PID_ |
| Difference in Best/Worst fit in highly fragmented memory | _Best Fit should trigger less of page faults_ |
