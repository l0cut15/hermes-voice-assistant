#pragma once
#include <stddef.h>

// POST transcript to Hermes agent API, fill response on success.
bool llm_complete(const char* transcript, char* response, size_t response_size);
