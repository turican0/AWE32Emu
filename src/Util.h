#pragma once
#include <cstring>
#include <string>
#include <cctype>
#include <clocale>
#include <libintl.h>

#define _(String) std::string{gettext(std::string{String}.c_str())}
