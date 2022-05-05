#pragma once

#include <cstdint>
#include <string>

extern "C" {
void mtrack_snapshot(const char* name = nullptr, size_t nameSize = 0);
void mtrack_disable_snapshots();
void mtrack_enable_snapshots();
}
