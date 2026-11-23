#pragma once

// Call before initializing SDL or loading a core. Throws if Linux filtering fails.
void restrict_runner_network();
