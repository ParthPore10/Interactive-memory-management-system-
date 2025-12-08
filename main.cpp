#include <iostream>
#include <unistd.h>  // for usleep
using namespace std;

// ---------- CONFIG ----------
#define FRAME_SIZE 5
#define NUMBER_OF_FRAMES 20
#define PAGES_PER_PROCESS 6
#define RAND_SEED 12345
#define MAX_BLOCKS 100
#define MAX_QUEUE 20

static int nextPID = 1;
static int randomSeed = RAND_SEED;

// ---------- STRUCTS ----------
struct memory_block {
    int start;
    int size;
    bool free;
    int pid;
    memory_block* next;
};

struct page_table_entry {
    int page_number;
    int frame_number;
    bool valid;
};

struct process {
    int pid;
    page_table_entry page_table[PAGES_PER_PROCESS];
    int page_count;
};

// ---------- COLOR MACROS ----------
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[36m"
#define BOLD    "\033[1m"

// ---------- ENUM ----------
enum FitType { FIRST_FIT = 1, BEST_FIT, WORST_FIT };

// ---------- REQUEST QUEUE ----------
struct Request {
    int type;      // 1 = allocate, 2 = deallocate
    int size;      // for allocate
    int strategy;  // 1/2/3 for alloc strategy
    int pid;       // for deallocate
};

struct Queue {
    Request arr[MAX_QUEUE];
    int front, rear, count;

    Queue() {
        front = 0;
        rear = -1;
        count = 0;
    }

    bool isFull()  { return count == MAX_QUEUE; }
    bool isEmpty() { return count == 0; }

    void enqueue(Request r) {
        if (isFull()) {
            cout << RED << "Request queue full! Cannot enqueue.\n" << RESET;
            return;
        }
        rear = (rear + 1) % MAX_QUEUE;
        arr[rear] = r;
        count++;
        cout << GREEN << "Request enqueued.\n" << RESET;
    }

    Request dequeue() {
        Request dummy;
        dummy.type = 0;
        dummy.size = 0;
        dummy.strategy = 0;
        dummy.pid = 0;

        if (isEmpty()) {
            cout << YELLOW << "Request queue empty.\n" << RESET;
            return dummy;
        }
        Request r = arr[front];
        front = (front + 1) % MAX_QUEUE;
        count--;
        return r;
    }

    void display() {
        cout << BLUE << "----- Request Queue (" << count << ") -----" << RESET << "\n";
        if (isEmpty()) {
            cout << YELLOW << "No pending requests.\n" << RESET;
            return;
        }
        int idx = front;
        for (int i = 0; i < count; i++) {
            Request r = arr[idx];
            if (r.type == 1) {
                cout << "  [ALLOC size=" << r.size
                     << ", strat=" << r.strategy << "]\n";
            } else if (r.type == 2) {
                cout << "  [FREE  pid=" << r.pid << "]\n";
            }
            idx = (idx + 1) % MAX_QUEUE;
        }
        cout << BLUE << "------------------------------" << RESET << "\n\n";
    }
};

// ---------- HEAP IMPLEMENTATIONS ----------
struct MinHeap {
    memory_block* arr[MAX_BLOCKS];
    int size;
    MinHeap() { size = 0; }

    void push(memory_block* blk) {
        arr[size] = blk;
        int i = size;
        size++;
        while (i > 0) {
            int parent = (i - 1) / 2;
            if ((*arr[parent]).size <= (*arr[i]).size) break;
            memory_block* tmp = arr[parent];
            arr[parent] = arr[i];
            arr[i] = tmp;
            i = parent;
        }
    }

    memory_block* top() { return size == 0 ? NULL : arr[0]; }

    void pop() {
        if (size == 0) return;
        arr[0] = arr[size - 1];
        size--;
        int i = 0;
        while (1) {
            int left = 2 * i + 1;
            int right = 2 * i + 2;
            int smallest = i;
            if (left < size && (*arr[left]).size < (*arr[smallest]).size) smallest = left;
            if (right < size && (*arr[right]).size < (*arr[smallest]).size) smallest = right;
            if (smallest == i) break;
            memory_block* tmp = arr[i];
            arr[i] = arr[smallest];
            arr[smallest] = tmp;
            i = smallest;
        }
    }

    void rebuild(memory_block* head) {
        size = 0;
        while (head) {
            if ((*head).free) push(head);
            head = (*head).next;
        }
    }
};

struct MaxHeap {
    memory_block* arr[MAX_BLOCKS];
    int size;
    MaxHeap() { size = 0; }

    void push(memory_block* blk) {
        arr[size] = blk;
        int i = size;
        size++;
        while (i > 0) {
            int parent = (i - 1) / 2;
            if ((*arr[parent]).size >= (*arr[i]).size) break;
            memory_block* tmp = arr[parent];
            arr[parent] = arr[i];
            arr[i] = tmp;
            i = parent;
        }
    }

    memory_block* top() { return size == 0 ? NULL : arr[0]; }

    void pop() {
        if (size == 0) return;
        arr[0] = arr[size - 1];
        size--;
        int i = 0;
        while (1) {
            int left = 2 * i + 1;
            int right = 2 * i + 2;
            int largest = i;
            if (left < size && (*arr[left]).size > (*arr[largest]).size) largest = left;
            if (right < size && (*arr[right]).size > (*arr[largest]).size) largest = right;
            if (largest == i) break;
            memory_block* tmp = arr[i];
            arr[i] = arr[largest];
            arr[largest] = tmp;
            i = largest;
        }
    }

    void rebuild(memory_block* head) {
        size = 0;
        while (head) {
            if ((*head).free) push(head);
            head = (*head).next;
        }
    }
};

// ---------- INIT ----------
void init_process(process* p, int pid) {
    (*p).pid = pid;
    (*p).page_count = 0;
    for (int i = 0; i < PAGES_PER_PROCESS; i++) {
        (*p).page_table[i].page_number = i;
        (*p).page_table[i].frame_number = -1;
        (*p).page_table[i].valid = false;
    }
}

// ---------- MEMORY ALLOCATION / DEALLOCATION ----------
void memory_allocation(memory_block* head, int requested_size, FitType fit) {
    if (requested_size <= 0) {
        cout << RED << "Requested size must be positive.\n" << RESET;
        return;
    }

    MinHeap minHeap;
    MaxHeap maxHeap;
    minHeap.rebuild(head);
    maxHeap.rebuild(head);
    memory_block* target = NULL;

    if (fit == FIRST_FIT) {
        memory_block* temp = head;
        while (temp) {
            if ((*temp).free && (*temp).size >= requested_size) {
                target = temp;
                break;
            }
            temp = (*temp).next;
        }
    } else if (fit == BEST_FIT) {
        while (minHeap.size > 0) {
            memory_block* blk = minHeap.top();
            if ((*blk).size >= requested_size && (*blk).free) {
                target = blk;
                break;
            }
            minHeap.pop();
        }
    } else if (fit == WORST_FIT) {
        while (maxHeap.size > 0) {
            memory_block* blk = maxHeap.top();
            if ((*blk).size >= requested_size && (*blk).free) {
                target = blk;
                break;
            }
            maxHeap.pop();
        }
    }

    if (!target) {
        cout << RED << "No suitable block found.\n" << RESET;
        return;
    }

    if ((*target).size == requested_size) {
        (*target).free = false;
        (*target).pid = nextPID++;
    } else {
        memory_block* newblock = new memory_block;
        (*newblock).start = (*target).start + requested_size;
        (*newblock).size = (*target).size - requested_size;
        (*newblock).free = true;
        (*newblock).pid = -1;
        (*newblock).next = (*target).next;

        (*target).next = newblock;
        (*target).size = requested_size;
        (*target).free = false;
        (*target).pid = nextPID++;
    }

    cout << GREEN << "Allocated PID " << (nextPID - 1)
         << " using " << (fit == FIRST_FIT ? "First Fit"
                                           : fit == BEST_FIT ? "Best Fit" : "Worst Fit")
         << RESET << "\n";
}

void deallocate_memory(int pid, memory_block* head) {
    memory_block* temp = head;
    memory_block* prev = NULL;
    while (temp) {
        if ((*temp).pid == pid) {
            (*temp).free = true;
            (*temp).pid = -1;
            cout << YELLOW << "Process " << pid << " deallocated.\n" << RESET;

            if ((*temp).next && (*(*temp).next).free) {
                (*temp).size += (*(*temp).next).size;
                (*temp).next = (*(*temp).next).next;
            }
            if (prev && (*prev).free) {
                (*prev).size += (*temp).size;
                (*prev).next = (*temp).next;
            }
            return;
        }
        prev = temp;
        temp = (*temp).next;
    }
    cout << RED << "PID not found.\n" << RESET;
}

// ---------- FRAME HELPERS ----------
void build_frame_view(memory_block* head, int frameOwner[NUMBER_OF_FRAMES]) {
    for (int i = 0; i < NUMBER_OF_FRAMES; i++) frameOwner[i] = -1;
    memory_block* temp = head;
    while (temp) {
        int startFrame = (*temp).start / FRAME_SIZE;
        int frameCount = (*temp).size / FRAME_SIZE;
        for (int i = 0; i < frameCount && startFrame + i < NUMBER_OF_FRAMES; i++) {
            if (!(*temp).free) frameOwner[startFrame + i] = (*temp).pid;
        }
        temp = (*temp).next;
    }
}

memory_block* allocate_one_frame_for_pid(memory_block* head, int ownerPid) {
    memory_block* temp = head;
    while (temp && (!(*temp).free || (*temp).size < FRAME_SIZE))
        temp = (*temp).next;
    if (!temp) return NULL;

    if ((*temp).size == FRAME_SIZE) {
        (*temp).free = false;
        (*temp).pid = ownerPid;
        return temp;
    }

    memory_block* newblock = new memory_block;
    (*newblock).start = (*temp).start + FRAME_SIZE;
    (*newblock).size = (*temp).size - FRAME_SIZE;
    (*newblock).free = true;
    (*newblock).pid = -1;
    (*newblock).next = (*temp).next;

    (*temp).next = newblock;
    (*temp).size = FRAME_SIZE;
    (*temp).free = false;
    (*temp).pid = ownerPid;
    return temp;
}

void allocate_pages_for_process(process* p, memory_block* head, int num_pages) {
    cout << "\nAllocating " << num_pages << " page(s) for Process " << (*p).pid << "...\n";
    for (int k = 0; k < num_pages; k++) {
        int idx = -1;
        for (int i = 0; i < PAGES_PER_PROCESS; i++) {
            if (!(*p).page_table[i].valid) { idx = i; break; }
        }
        if (idx == -1) { cout << "Page table full\n"; break; }

        memory_block* frameBlock = allocate_one_frame_for_pid(head, (*p).pid);
        if (!frameBlock) { cout << "No free frame\n"; break; }

        int frameNumber = (*frameBlock).start / FRAME_SIZE;
        (*p).page_table[idx].frame_number = frameNumber;
        (*p).page_table[idx].valid = true;
        (*p).page_count++;
        cout << "  Page " << idx << " -> Frame " << frameNumber << "\n";
    }
}

// ---------- DISPLAY ----------
void clear_screen() { cout << "\033[2J\033[H"; }

void display_memory_bar(memory_block* head) {
    int frameOwner[NUMBER_OF_FRAMES];
    build_frame_view(head, frameOwner);
    cout << BOLD << "Physical Memory (Frames):" << RESET << "\n";
    for (int i = 0; i < NUMBER_OF_FRAMES; i++) {
        if (frameOwner[i] == -1) cout << GREEN << "[  ]" << RESET;
        else cout << RED << "[P" << frameOwner[i] << "]" << RESET;
    }
    cout << "\n";
    for (int i = 0; i < NUMBER_OF_FRAMES; i++) {
        if (i < 10) cout << "  " << i << " ";
        else        cout << " " << i << " ";
    }
    cout << "\n\n";
}

void display_page_table(process* p) {
    cout << BOLD << "Page Table (Process " << (*p).pid << "):" << RESET << "\n";
    cout << "Page\tFrame\tValid\n";
    for (int i = 0; i < PAGES_PER_PROCESS; i++) {
        cout << (*p).page_table[i].page_number << "\t"
             << (*p).page_table[i].frame_number << "\t"
             << ((*p).page_table[i].valid ? GREEN "1" RESET : RED "0" RESET) << "\n";
    }
    cout << "-------------------------\n\n";
}

void draw_bar(int filled, int total, const char* color) {
    int width = 20;
    int filledBlocks = (filled * width) / (total > 0 ? total : 1);
    cout << color;
    for (int i = 0; i < width; i++)
        cout << (i < filledBlocks ? "█" : "░");
    cout << RESET;
}

void draw_heatmap(float utilization, int tick) {
    static const char* gradient[] = {"▁","▂","▃","▄","▅","▆","▇","█"};
    int len = 24;
    const char* color = (utilization < 0.4f) ? GREEN
                        : (utilization < 0.7f) ? YELLOW : RED;
    cout << " Memory Heatmap: " << color;
    for (int i = 0; i < len; i++) {
        int idx = (tick + i) % 8;
        if (i < utilization * len) cout << gradient[idx];
        else cout << " ";
    }
    cout << RESET << "\n";
}

void display_stats(memory_block* head, process* p, int tick) {
    int frameOwner[NUMBER_OF_FRAMES];
    build_frame_view(head, frameOwner);
    int used = 0;
    for (int i = 0; i < NUMBER_OF_FRAMES; i++)
        if (frameOwner[i] != -1) used++;
    int freeFrames = NUMBER_OF_FRAMES - used;
    float util = (float)used / NUMBER_OF_FRAMES;

    cout << BLUE << "************ Stats *************" << RESET << "\n";
    cout << " Used Frames : "; draw_bar(used, NUMBER_OF_FRAMES, RED);
    cout << " " << used << "/" << NUMBER_OF_FRAMES << "\n";
    cout << " Free Frames : "; draw_bar(freeFrames, NUMBER_OF_FRAMES, GREEN);
    cout << " " << freeFrames << "/" << NUMBER_OF_FRAMES << "\n";
    cout << " Process Pages: "; draw_bar((*p).page_count, PAGES_PER_PROCESS, YELLOW);
    cout << " " << (*p).page_count << "/" << PAGES_PER_PROCESS << "\n";
    draw_heatmap(util, tick);
    cout << BLUE << "********************************" << RESET << "\n\n";
}

// ---------- FRAGMENTATION ANALYSIS ----------
void fragmentation_analysis(memory_block* head) {
    int totalFree = 0;
    int freeBlocks = 0;
    int largestFree = 0;

    memory_block* temp = head;
    while (temp) {
        if ((*temp).free) {
            totalFree += (*temp).size;
            freeBlocks++;
            if ((*temp).size > largestFree)
                largestFree = (*temp).size;
        }
        temp = (*temp).next;
    }

    cout << BLUE << "--------- Fragmentation Analysis ---------" << RESET << "\n";
    if (freeBlocks == 0) {
        cout << RED << "No free blocks — memory fully allocated!\n" << RESET;
        cout << BLUE << "----------------------------------------" << RESET << "\n\n";
        return;
    }

    float avgFree = (float)totalFree / freeBlocks;
    float fragRatio = (float)(totalFree - largestFree) / totalFree;

    cout << " Total Free Memory     : " << totalFree << "\n";
    cout << " Number of Free Blocks : " << freeBlocks << "\n";
    cout << " Largest Free Block    : " << largestFree << "\n";
    cout << " Average Free Block    : " << avgFree << "\n";
    cout << " Fragmentation Ratio   : " << fragRatio << "\n";
    cout << BLUE << "--------------------------------" << RESET << "\n\n";
}

// ---------- MAIN ----------
int main() {
    memory_block* head = new memory_block{0, 100, true, -1, NULL};
    process p1; init_process(&p1, 1);
    Queue requestQueue;

    char choice;
    int tick = 0;

    while (1) {
        tick++;
        clear_screen();

        cout << BOLD << BLUE
             << "Interactive Memory Management System (IMMS)\n"
             << RESET << "\n";

        // Visual dashboard
        display_memory_bar(head);
        display_page_table(&p1);
        display_stats(head, &p1, tick);

        // Show queued requests
        requestQueue.display();

        cout << BOLD << "Menu:" << RESET << "\n";
        cout << " 1 - Immediate Allocate (First/Best/Worst)\n";
        cout << " 2 - Immediate Deallocate by PID\n";
        cout << " 3 - Allocate Pages for Process 1\n";
        cout << " 4 - Fragmentation Analysis\n";
        cout << " 5 - Enqueue Allocation Request\n";
        cout << " 6 - Enqueue Deallocation Request\n";
        cout << " 7 - Process Next Request (from Queue)\n";
        cout << " 8 - Run All Queued Requests (Auto Mode)\n";
        cout << " q - Quit\n";
        cout << "Enter choice: ";
        cin >> choice;

        if (choice == '1') {
            int sz, strategy;
            cout << "Enter size: "; cin >> sz;
            cout << "Choose strategy (1-First,2-Best,3-Worst): "; cin >> strategy;
            if (strategy < 1 || strategy > 3) strategy = 1;
            memory_allocation(head, sz, (FitType)strategy);
        }
        else if (choice == '2') {
            int pid; cout << "Enter PID: "; cin >> pid;
            deallocate_memory(pid, head);
        }
        else if (choice == '3') {
            int n; cout << "Enter number of pages: "; cin >> n;
            allocate_pages_for_process(&p1, head, n);
        }
        else if (choice == '4') {
            fragmentation_analysis(head);
        }
        else if (choice == '5') {
            Request r;
            r.type = 1;
            cout << "Enter size: "; cin >> r.size;
            cout << "Strategy (1-First,2-Best,3-Worst): "; cin >> r.strategy;
            r.pid = 0;
            requestQueue.enqueue(r);
        }
        else if (choice == '6') {
            Request r;
            r.type = 2;
            cout << "Enter PID to deallocate: "; cin >> r.pid;
            r.size = 0;
            r.strategy = 0;
            requestQueue.enqueue(r);
        }
        else if (choice == '7') {
            Request r = requestQueue.dequeue();
            if (r.type == 1) {
                memory_allocation(head, r.size, (FitType)r.strategy);
            } else if (r.type == 2) {
                deallocate_memory(r.pid, head);
            }
        }
        else if (choice == '8') {
            cout << YELLOW << "Running all queued requests automatically...\n" << RESET;
            usleep(300000);

            while (!requestQueue.isEmpty()) {
                Request r = requestQueue.dequeue();

                if (r.type == 1)
                    memory_allocation(head, r.size, (FitType)r.strategy);
                else if (r.type == 2)
                    deallocate_memory(r.pid, head);

                clear_screen();
                cout << BOLD << BLUE
                     << "Interactive Memory Management System (Auto Mode)\n"
                     << RESET << "\n";
                display_memory_bar(head);
                display_page_table(&p1);
                display_stats(head, &p1, tick++);
                requestQueue.display();

                usleep(500000); // animation delay
            }

            cout << GREEN << "\nAll queued requests processed!\n" << RESET;
            usleep(500000);
        }
        else if (choice == 'q' || choice == 'Q') {
            break;
        }

        usleep(180000);
    }
    return 0;
}