# Run inside the Daxa source dir. Clang rejects std::optional<FileCache>::emplace() because the
# nested struct's default member initializers are not yet usable inside the enclosing class.
set(_file src/utils/impl_pipeline_manager.hpp)
file(READ ${_file} _content)
string(REPLACE "std::shared_mutex mtx = {};" "std::shared_mutex mtx;" _content "${_content}")
string(REPLACE "std::unordered_map<std::string, FileCacheEntry> files = {};" "std::unordered_map<std::string, FileCacheEntry> files;" _content "${_content}")
file(WRITE ${_file} "${_content}")
