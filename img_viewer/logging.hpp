#pragma once

#include "stdio.h"
#include "stdarg.h"
#include "basic_typedefs.hpp"
#include "preprocessor_stuff.hpp"

#include <string>
typedef std::string str;
typedef std::string const& strcr;

#include <vector>

#include "prints.hpp"

struct Logger {
	enum log_type_e { INFO, WARNING, ERROR_ };
	
	struct Log_Line {
		log_type_e		type;
		std::string		line;

		Log_Line (log_type_e t, strcr l): type{t}, line{l} {} 
	};
	
	std::vector<Log_Line> loglines;

	void vlog (log_type_e type, cstr format, va_list vl) {
		
		str line;
		vprints(&line, format, vl);

		loglines.emplace_back(type, line);
	}
};

Logger g_logger;

void vwarning_log (cstr format, va_list& vl) {
	vfprintf(stderr, format, vl);

	g_logger.vlog(Logger::WARNING, format, vl);
}
void warning_log (cstr format, ...) {
	va_list vl;
	va_start(vl, format);

	vwarning_log(format, vl);

	va_end(vl);
}

void failed_assert_log (cstr cond_str) {
	warning_log("Assertion failed! \"%s\"\n", cond_str);

	DBGBREAK;
}
void failed_assert_log (cstr cond_str, cstr format, ...) {
	va_list vl;
	va_start(vl, format);

	warning_log("Assertion failed! \"%s\" msg: ", cond_str);

	vwarning_log(format, vl);

	warning_log("\n");

	va_end(vl);

	DBGBREAK;
}
#define assert_log(cond, ...) if (!(cond)) failed_assert_log(STRINGIFY(cond), __VA_ARGS__)
