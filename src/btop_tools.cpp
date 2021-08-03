/* Copyright 2021 Aristocratos (jakob@qvantnet.com)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/

#include <cmath>
#include <iostream>
#include <fstream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <utility>
#include <robin_hood.h>

#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <btop_shared.hpp>
#include <btop_tools.hpp>

using std::string_view, std::array, std::max, std::floor, std::to_string, std::cin, std::cout, std::flush, robin_hood::unordered_flat_map;
namespace fs = std::filesystem;
namespace rng = std::ranges;

//? ------------------------------------------------- NAMESPACES ------------------------------------------------------

//* Collection of escape codes and functions for terminal manipulation
namespace Term {

	atomic<bool> initialized = false;
	atomic<int> width = 0;
	atomic<int> height = 0;
	string current_tty;

	namespace {
		struct termios initial_settings;

		//* Toggle terminal input echo
		bool echo(bool on=true) {
			struct termios settings;
			if (tcgetattr(STDIN_FILENO, &settings)) return false;
			if (on) settings.c_lflag |= ECHO;
			else settings.c_lflag &= ~(ECHO);
			return 0 == tcsetattr(STDIN_FILENO, TCSANOW, &settings);
		}

		//* Toggle need for return key when reading input
		bool linebuffered(bool on=true) {
			struct termios settings;
			if (tcgetattr(STDIN_FILENO, &settings)) return false;
			if (on) settings.c_lflag |= ICANON;
			else settings.c_lflag &= ~(ICANON);
			if (tcsetattr(STDIN_FILENO, TCSANOW, &settings)) return false;
			if (on) setlinebuf(stdin);
			else setbuf(stdin, NULL);
			return true;
		}
	}

	bool refresh() {
		struct winsize w;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		if (width != w.ws_col or height != w.ws_row) {
			width = w.ws_col;
			height = w.ws_row;
			return true;
		}
		return false;
	}

	bool init() {
		if (not initialized) {
			initialized = (bool)isatty(STDIN_FILENO);
			if (initialized) {
				tcgetattr(STDIN_FILENO, &initial_settings);
				current_tty = (string)ttyname(STDIN_FILENO);
				cin.sync_with_stdio(false);
				cin.tie(NULL);
				echo(false);
				linebuffered(false);
				refresh();
				cout << alt_screen << hide_cursor << mouse_on << flush;
				Global::resized = false;
			}
		}
		return initialized;
	}

	void restore() {
		if (initialized) {
			echo(true);
			linebuffered(true);
			tcsetattr(STDIN_FILENO, TCSANOW, &initial_settings);
			cout << Term::mouse_off << Term::normal_screen << Term::show_cursor << flush;
			initialized = false;
		}
	}
}

//? --------------------------------------------------- FUNCTIONS -----------------------------------------------------

namespace Tools {

	atomic<int> active_locks (0);

	string uresize(string str, const size_t len, const bool wide) {
		if (len < 1 or str.empty()) return "";
		for (size_t x = 0, i = 0; i < str.size(); i++) {
			if (wide and static_cast<unsigned char>(str.at(i)) > 0xef) x += 2;
			else if ((static_cast<unsigned char>(str.at(i)) & 0xC0) != 0x80) x++;
			if (x >= len + 1) {
				str.resize(i);
				str.shrink_to_fit();
				break;
			}
		}
		return str;
	}

	string luresize(string str, const size_t len, const bool wide) {
		if (len < 1 or str.empty()) return "";
		for (size_t x = 0, last_pos = 0, i = str.size() - 1; i > 0 ; i--) {
			if (wide and static_cast<unsigned char>(str.at(i)) > 0xef) {
				x += 2;
				last_pos = max(0ul, i - 1);
			}
			else if ((static_cast<unsigned char>(str.at(i)) & 0xC0) != 0x80) {
				x++;
				last_pos = i;
			}
			if (x >= len) {
				str = str.substr(last_pos);
				str.shrink_to_fit();
				break;
			}
		}
		return str;
	}

	string ltrim(const string& str, const string& t_str) {
		string_view str_v = str;
		while (str_v.starts_with(t_str)) str_v.remove_prefix(t_str.size());
		return (string)str_v;
	}

	string rtrim(const string& str, const string& t_str) {
		string_view str_v = str;
		while (str_v.ends_with(t_str)) str_v.remove_suffix(t_str.size());
		return (string)str_v;
	}

	auto ssplit(const string& str, const char& delim) -> vector<string> {
		vector<string> out;
		for (const auto& s : str 	| rng::views::split(delim)
									| rng::views::transform([](auto &&rng) {
										return string_view(&*rng.begin(), rng::distance(rng));
		})) {
			if (not s.empty()) out.emplace_back(s);
		}
		return out;
	}

	string ljust(string str, const size_t x, const bool utf, const bool wide, const bool limit) {
		if (utf) {
			if (limit and ulen(str, wide) > x) return uresize(str, x, wide);
			return str + string(max((int)(x - ulen(str)), 0), ' ');
		}
		else {
			if (limit and str.size() > x) { str.resize(x); return str; }
			return str + string(max((int)(x - str.size()), 0), ' ');
		}
	}

	string rjust(string str, const size_t x, const bool utf, const bool wide, const bool limit) {
		if (utf) {
			if (limit and ulen(str, wide) > x) return uresize(str, x, wide);
			return string(max((int)(x - ulen(str)), 0), ' ') + str;
		}
		else {
			if (limit and str.size() > x) { str.resize(x); return str; };
			return string(max((int)(x - str.size()), 0), ' ') + str;
		}
	}

	string cjust(string str, const size_t x, const bool utf, const bool wide, const bool limit) {
		if (utf) {
			if (limit and ulen(str, wide) > x) return uresize(str, x, wide);
			return string(max((int)ceil((double)(x - ulen(str)) / 2), 0), ' ') + str + string(max((int)floor((double)(x - ulen(str)) / 2), 0), ' ');
		}
		else {
			if (limit and str.size() > x) { str.resize(x); return str; }
			return string(max((int)ceil((double)(x - str.size()) / 2), 0), ' ') + str + string(max((int)floor((double)(x - str.size()) / 2), 0), ' ');
		}
	}

	string trans(const string& str) {
		string_view oldstr = str;
		string newstr;
		newstr.reserve(str.size());
		for (size_t pos; (pos = oldstr.find(' ')) != string::npos;) {
			newstr.append(oldstr.substr(0, pos));
			size_t x = 0;
			while (pos + x < oldstr.size() and oldstr.at(pos + x) == ' ') x++;
			newstr.append(Mv::r(x));
			oldstr.remove_prefix(pos + x);
		}
		return (newstr.empty()) ? str : newstr + (string)oldstr;
	}

	string sec_to_dhms(size_t seconds) {
		size_t days = seconds / 86400; seconds %= 86400;
		size_t hours = seconds / 3600; seconds %= 3600;
		size_t minutes = seconds / 60; seconds %= 60;
		string out 	= (days > 0 ? to_string(days) + "d " : "")
					+ (hours < 10 ? "0" : "") + to_string(hours) + ":"
					+ (minutes < 10 ? "0" : "") + to_string(minutes) + ":"
					+ (seconds < 10 ? "0" : "") + to_string(seconds);
		return out;
	}

	string floating_humanizer(uint64_t value, const bool shorten, size_t start, const bool bit, const bool per_second) {
		string out;
		const size_t mult = (bit) ? 8 : 1;
		static const array<string, 11> Units_bit = {"bit", "Kib", "Mib", "Gib", "Tib", "Pib", "Eib", "Zib", "Yib", "Bib", "GEb"};
		static const array<string, 11> Units_byte = {"Byte", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB", "BiB", "GEB"};
		const auto& units = (bit) ? Units_bit : Units_byte;

		value *= 100 * mult;

		while (value >= 102400) {
			value >>= 10;
			if (value < 100) {
				out = to_string(value);
				break;
			}
			start++;
		}
		if (out.empty()) {
			out = to_string(value);
			if (out.size() == 4 and start > 0) { out.pop_back(); out.insert(2, ".");}
			else if (out.size() == 3 and start > 0) out.insert(1, ".");
			else if (out.size() >= 2) out.resize(out.size() - 2);
		}
		if (shorten) {
				if (out.find('.') != string::npos) out = to_string((int)round(stof(out)));
				if (out.size() > 3) { out = to_string((int)(out[0] - '0') + 1); start++;}
				out.push_back(units[start][0]);
		}
		else out += " " + units[start];

		if (per_second) out += (bit) ? "ps" : "/s";
		return out;
	}

	std::string operator*(const string& str, int64_t n) {
		if (n < 1 or str.empty()) return "";
		else if(n == 1) return str;
		string new_str;
		new_str.reserve(str.size() * n);
		for (; n > 0; n--) new_str.append(str);
		return new_str;
	}

	string strf_time(const string& strf) {
		auto in_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm bt {};
		std::stringstream ss;
		ss << std::put_time(localtime_r(&in_time_t, &bt), strf.c_str());
		return ss.str();
	}

	atomic_lock::atomic_lock(atomic<bool>& atom) : atom(atom) {
		active_locks++;
		while (not this->atom.compare_exchange_strong(this->not_true, true));
	}

	atomic_lock::~atomic_lock() {
		active_locks--;
		this->atom.store(false);
		atomic_notify(this->atom);
	}

	string readfile(const std::filesystem::path& path, const string& fallback) {
		if (not fs::exists(path)) return fallback;
		string out;
		try {
			std::ifstream file(path);
			for (string readstr; getline(file, readstr); out += readstr);
		}
		catch (const std::exception& e) {
			throw std::runtime_error("Exception when reading " + (string)path + " : " + e.what());
		}
		return (out.empty() ? fallback : out);
	}

	auto celsius_to(const long long& celsius, const string& scale) -> tuple<long long, string> {
		if (scale == "celsius")
			return {celsius, "°C"};
		else if (scale == "fahrenheit")
			return {(long long)round((double)celsius * 1.8 + 32), "°F"};
		else if (scale == "kelvin")
			return {(long long)round((double)celsius + 273.15), "K "};
		else if (scale == "rankine")
			return {(long long)round((double)celsius * 1.8 + 491.67), "°R"};
		return {0, ""};
	}

}

namespace Logger {
	using namespace Tools;
	namespace {
		std::atomic<bool> busy (false);
		bool first = true;
		const string tdf = "%Y/%m/%d (%T) | ";
	}

	size_t loglevel;
	fs::path logfile;

	void set(const string& level) {
		loglevel = v_index(log_levels, level);
	}

	void log_write(const size_t level, const string& msg) {
		if (loglevel < level or logfile.empty()) return;
		atomic_lock lck(busy);
		std::error_code ec;
		try {
			if (fs::exists(logfile) and fs::file_size(logfile, ec) > 1024 << 10 and not ec) {
				auto old_log = logfile;
				old_log += ".1";
				if (fs::exists(old_log)) fs::remove(old_log, ec);
				if (not ec) fs::rename(logfile, old_log, ec);
			}
			if (not ec) {
				std::ofstream lwrite(logfile, std::ios::app);
				if (first) { first = false; lwrite << "\n" << strf_time(tdf) << "===> btop++ v." << Global::Version << "\n";}
				lwrite << strf_time(tdf) << log_levels.at(level) << ": " << msg << "\n";
			}
			else logfile.clear();
		}
		catch (const std::exception& e) {
			throw std::runtime_error("Exception in Logger::log_write() : " + (string)e.what());
		}
	}
}
