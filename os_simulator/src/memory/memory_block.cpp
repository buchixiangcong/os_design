#include "memory/memory_block.h"
#include <sstream>

std::string MemoryBlock::to_string() const {
    std::ostringstream oss;
    if (is_free) {
        oss << "[空闲 " << size << "KB @" << start_addr
            << "-" << end_addr() << "]";
    } else {
        oss << "[已分配 " << size << "KB @" << start_addr
            << "-" << end_addr() << " PID:" << pid << "]";
    }
    return oss.str();
}
