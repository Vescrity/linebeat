#pragma once
#include <string>

#include "model.h"

namespace project_io {

std::string toJsonString(const Project& p);
bool fromJsonString(Project& p, const std::string& text, std::string* error,
                    int* expressionErrors = nullptr);

bool save(const Project& p, const std::string& path, std::string* error);
bool load(Project& p, const std::string& path, std::string* error,
          int* expressionErrors = nullptr);

// 干净的默认场景：一条线，一个球
Project defaultProject();

} // namespace project_io
