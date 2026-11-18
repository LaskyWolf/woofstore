#pragma once
#include "rust/cxx.h"
#include <QString>
int run_shell(rust::Str url, rust::Str link);
bool message_box(int kind, const QString &title, const QString &text);
