#pragma once

bool is_lower (char c) {
	return c >= 'a' && c <= 'z';
}
bool is_upper (char c) {
	return c >= 'A' && c <= 'Z';
}

char to_lower (char c) {
	return is_upper(c) ? c +('a' -'A') : c;
}
char to_upper (char c) {
	return is_lower(c) ? c +('A' -'a') : c;
}
