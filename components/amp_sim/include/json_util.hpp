#pragma once
#include <string>

// Kleine JSON-Hilfen (Ausgabe wird von Hand gebaut, damit Zahlenformat und Groesse kontrolliert bleiben)
std::string json_escape(const std::string &s);
void json_appendf(std::string &out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
