// Copyright (C) 2020 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "argprocessing.hpp"

#include "Arg.hpp"
#include "Context.hpp"
#include "FormatNonstdStringView.hpp"
#include "Logging.hpp"
#include "assertions.hpp"
#include "compopt.hpp"
#include "language.hpp"

#include <cassert>

using Logging::log;
using nonstd::nullopt;
using nonstd::optional;
using nonstd::string_view;

namespace {

enum class ColorDiagnostics : int8_t { never, automatic, always };

struct ArgumentProcessingState
{
  bool found_c_opt = false;
  bool found_dc_opt = false;
  bool found_S_opt = false;
  bool found_pch = false;
  bool found_fpch_preprocess = false;
  ColorDiagnostics color_diagnostics = ColorDiagnostics::automatic;
  bool found_directives_only = false;
  bool found_rewrite_includes = false;

  std::string explicit_language;    // As specified with -x.
  std::string file_language;        // As deduced from file extension.
  std::string input_charset_option; // -finput-charset=...

  // Is the dependency makefile name overridden with -MF?
  bool dependency_filename_specified = false;

  // Is the dependency target name implicitly specified using
  // DEPENDENCIES_OUTPUT or SUNPRO_DEPENDENCIES?
  bool dependency_implicit_target_specified = false;

  // Is the compiler being asked to output debug info on level 3?
  bool generating_debuginfo_level_3 = false;

  // common_args contains all original arguments except:
  // * those that never should be passed to the preprocessor,
  // * those that only should be passed to the preprocessor (if run_second_cpp
  //   is false), and
  // * dependency options (like -MD and friends).
  Args common_args;

  // cpp_args contains arguments that were not added to common_args, i.e. those
  // that should only be passed to the preprocessor if run_second_cpp is false.
  // If run_second_cpp is true, they will be passed to the compiler as well.
  Args cpp_args;

  // dep_args contains dependency options like -MD. They are only passed to the
  // preprocessor, never to the compiler.
  Args dep_args;

  // compiler_only_args contains arguments that should only be passed to the
  // compiler, not the preprocessor.
  Args compiler_only_args;
};

bool
color_output_possible()
{
  const char* term_env = getenv("TERM");
  return isatty(STDERR_FILENO) && term_env && strcasecmp(term_env, "DUMB") != 0;
}

bool
detect_pch(Context& ctx,
           const std::string& option,
           const std::string& arg,
           bool is_cc1_option,
           bool* found_pch)
{
  ASSERT(found_pch);

  // Try to be smart about detecting precompiled headers.
  // If the option is an option for Clang (is_cc1_option), don't accept
  // anything just because it has a corresponding precompiled header,
  // because Clang doesn't behave that way either.
  std::string pch_file;
  if (option == "-include-pch" || option == "-include-pth") {
    if (Stat::stat(arg)) {
      log("Detected use of precompiled header: {}", arg);
      pch_file = arg;
    }
  } else if (!is_cc1_option) {
    for (const auto& extension : {".gch", ".pch", ".pth"}) {
      std::string path = arg + extension;
      if (Stat::stat(path)) {
        log("Detected use of precompiled header: {}", path);
        pch_file = path;
      }
    }
  }

  if (!pch_file.empty()) {
    if (!ctx.included_pch_file.empty()) {
      log("Multiple precompiled headers used: {} and {}",
          ctx.included_pch_file,
          pch_file);
      return false;
    }
    ctx.included_pch_file = pch_file;
    *found_pch = true;
  }
  return true;
}

bool
process_profiling_option(Context& ctx, const std::string& argStr)
{
  static const std::vector<std::string> known_simple_options = {
    "-fprofile-correction",
    "-fprofile-reorder-functions",
    "-fprofile-sample-accurate",
    "-fprofile-values",
  };

  if (std::find(
        known_simple_options.begin(), known_simple_options.end(), argStr)
      != known_simple_options.end()) {
    return true;
  }

  const auto arg = Arg(argStr);
  nonstd::string_view new_profile_path;
  bool new_profile_use = false;

  if (arg.key() == "-fprofile-dir") {
    new_profile_path = arg.value();
  } else if (arg == "-fprofile-generate" || arg == "-fprofile-instr-generate") {
    ctx.args_info.profile_generate = true;
    if (ctx.guessed_compiler == GuessedCompiler::clang) {
      new_profile_path = ".";
    } else {
      // GCC uses $PWD/$(basename $obj).
      new_profile_path = ctx.apparent_cwd;
    }
  } else if (arg.key() == "-fprofile-generate"
             || arg.key() == "-fprofile-instr-generate") {
    ctx.args_info.profile_generate = true;
    new_profile_path = arg.value();
  } else if (arg == "-fprofile-use" || arg == "-fprofile-instr-use"
             || arg == "-fprofile-sample-use" || arg == "-fbranch-probabilities"
             || arg == "-fauto-profile") {
    new_profile_use = true;
    if (ctx.args_info.profile_path.empty()) {
      new_profile_path = ".";
    }
  } else if (arg.key() == "-fprofile-use" || arg.key() == "-fprofile-instr-use"
             || arg.key() == "-fprofile-sample-use"
             || arg.key() == "-fauto-profile") {
    new_profile_use = true;
    new_profile_path = arg.value();
  } else {
    log("Unknown profiling option: {}", arg.full());
    return false;
  }

  if (new_profile_use) {
    if (ctx.args_info.profile_use) {
      log("Multiple profiling options not supported");
      return false;
    }
    ctx.args_info.profile_use = true;
  }

  if (!new_profile_path.empty()) {
    ctx.args_info.profile_path = std::string(new_profile_path);
    log("Set profile directory to {}", ctx.args_info.profile_path);
  }

  if (ctx.args_info.profile_generate && ctx.args_info.profile_use) {
    // Too hard to figure out what the compiler will do.
    log("Both generating and using profile info, giving up");
    return false;
  }

  return true;
}

// The compiler is invoked with the original arguments in the depend mode.
// Collect extra arguments that should be added.
void
add_depend_mode_extra_original_args(Context& ctx, const std::string& arg)
{
  if (ctx.config.depend_mode()) {
    ctx.args_info.depend_extra_args.push_back(arg);
  }
}

optional<Statistic>
process_arg(Context& ctx,
            Args& args,
            size_t& args_index,
            ArgumentProcessingState& state)
{
  ArgsInfo& args_info = ctx.args_info;
  Config& config = ctx.config;

  size_t& i = args_index;

  // The user knows best: just swallow the next arg.
  if (args[i].key() == "--ccache-skip") {
    state.common_args.push_back(args[i].value());
    return nullopt;
  }

  // Special case for -E.
  if (args[i] == "-E") {
    return Statistic::called_for_preprocessing;
  }

  // Handle "@file" argument.
  if (Util::starts_with(args[i], "@") || Util::starts_with(args[i], "-@")) {
    const char* argpath = args[i].full().c_str() + 1;

    if (argpath[-1] == '-') {
      ++argpath;
    }
    auto file_args = Args::from_gcc_atfile(argpath);
    if (!file_args) {
      log("Couldn't read arg file {}", argpath);
      return Statistic::bad_compiler_arguments;
    }

    args.replace(i, *file_args);
    i--;
    return nullopt;
  }

  // Handle cuda "-optf" and "--options-file" argument.
  if (ctx.guessed_compiler == GuessedCompiler::nvcc
      && (args[i].key() == "-optf" || args[i].key() == "--options-file")) {
    // Argument is a comma-separated list of files.
    const auto paths = Util::split_into_strings(args[i].value(), ",");
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
      const auto file_args = Args::from_gcc_atfile(*it);
      if (!file_args) {
        log("Couldn't read CUDA options file {}", *it);
        return Statistic::bad_compiler_arguments;
      }

      args.insert(i + 1, *file_args);
    }

    return nullopt;
  }

  // These are always too hard.
  if (compopt_too_hard(args[i]) || Util::starts_with(args[i], "-fdump-")
      || Util::starts_with(args[i], "-MJ")) {
    log("Compiler option {} is unsupported", args[i]);
    return Statistic::unsupported_compiler_option;
  }

  // These are too hard in direct mode.
  if (config.direct_mode() && compopt_too_hard_for_direct_mode(args[i])) {
    log("Unsupported compiler option for direct mode: {}", args[i]);
    config.set_direct_mode(false);
  }

  // -Xarch_* options are too hard.
  if (Util::starts_with(args[i], "-Xarch_")) {
    log("Unsupported compiler option: {}", args[i]);
    return Statistic::unsupported_compiler_option;
  }

  // Handle -arch options.
  if (args[i].key() == "-arch") {
    args_info.arch_args.emplace_back(args[i].value());
    if (args_info.arch_args.size() == 2) {
      config.set_run_second_cpp(true);
    }
    return nullopt;
  }

  // Some arguments that clang passes directly to cc1 (related to precompiled
  // headers) need the usual ccache handling. In those cases, the -Xclang
  // prefix is skipped and the cc1 argument is handled instead.
  if (args[i] == "-Xclang" && i < args.size() - 1
      && (args[i + 1] == "-emit-pch" || args[i + 1] == "-emit-pth"
          || args[i + 1] == "-include-pch" || args[i + 1] == "-include-pth"
          || args[i + 1] == "-fno-pch-timestamp")) {
    if (compopt_affects_comp(std::string(args[i + 1]))) {
      state.compiler_only_args.push_back("-Xclang");
    } else if (compopt_affects_cpp(std::string(args[i + 1]))) {
      state.cpp_args.push_back("-Xclang");
    } else {
      state.common_args.push_back("-Xclang");
    }
    ++i;
  }

  // Handle options that should not be passed to the preprocessor.
  if (compopt_affects_comp(args[i])) {
    state.compiler_only_args.push_back(args[i]);
    if (compopt_takes_arg(args[i])
        || (ctx.guessed_compiler == GuessedCompiler::nvcc
            && args[i] == "-Werror")) {
      if (i == args.size() - 1) {
        log("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      state.compiler_only_args.push_back(args[i + 1]);
      ++i;
    }
    return nullopt;
  }
  if (compopt_prefix_affects_comp(args[i])) {
    state.compiler_only_args.push_back(args[i]);
    return nullopt;
  }

  // Modules are handled on demand as necessary in the background, so there is
  // no need to cache them, they can in practice be ignored. All that is needed
  // is to correctly depend also on module.modulemap files, and those are
  // included only in depend mode (preprocessed output does not list them).
  // Still, not including the modules themselves in the hash could possibly
  // result in an object file that would be different from the actual
  // compilation (even though it should be compatible), so require a sloppiness
  // flag.
  if (args[i] == "-fmodules") {
    if (!config.depend_mode() || !config.direct_mode()) {
      log("Compiler option {} is unsupported without direct depend mode",
          args[i]);
      return Statistic::could_not_use_modules;
    } else if (!(config.sloppiness() & SLOPPY_MODULES)) {
      log(
        "You have to specify \"modules\" sloppiness when using"
        " -fmodules to get hits");
      return Statistic::could_not_use_modules;
    }
  }

  // We must have -c.
  if (args[i] == "-c") {
    state.found_c_opt = true;
    return nullopt;
  }

  // when using nvcc with separable compilation, -dc implies -c
  if ((args[i] == "-dc" || args[i] == "--device-c")
      && ctx.guessed_compiler == GuessedCompiler::nvcc) {
    state.found_dc_opt = true;
    return nullopt;
  }

  // -S changes the default extension.
  if (args[i] == "-S") {
    state.common_args.push_back(args[i]);
    state.found_S_opt = true;
    return nullopt;
  }

  if (args[i].key() == "-x") {
    // -xCODE (where CODE can be e.g. Host or CORE-AVX2, always starting with an
    // uppercase letter) is an ordinary Intel compiler option, not a language
    // specification. (GCC's "-x" language argument is always lowercase.)
    if (!islower(args[i].value()[0])) {
      state.common_args.push_back(args[i]);
    }

    // Special handling for -x: remember the last specified language before the
    // input file and strip all -x options from the arguments.
    else {
      if (args_info.input_file.empty()) {
        state.explicit_language = std::string(args[i].value());
      }
    }
    return nullopt;
  }

  // We need to work out where the output was meant to go.
  if (args[i] == "-o") {
    if (i == args.size() - 1) {
      log("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    args_info.output_obj = Util::make_relative_path(ctx, args[i + 1]);
    i++;
    return nullopt;
  }

  // Alternate form of -o with no space. Nvcc does not support this.
  if (Util::starts_with(args[i], "-o")
      && ctx.guessed_compiler != GuessedCompiler::nvcc) {
    args_info.output_obj =
      Util::make_relative_path(ctx, string_view(args[i]).substr(2));
    return nullopt;
  }

  const auto arg = Arg(args[i]);
  if (arg.key() == "-fdebug-prefix-map" || arg.key() == "-ffile-prefix-map") {
    args_info.debug_prefix_maps.emplace_back(arg.value());
    state.common_args.push_back(arg);
    return nullopt;
  }

  // Debugging is handled specially, so that we know if we can strip line
  // number info.
  if (Util::starts_with(arg.full(), "-g")) {
    state.common_args.push_back(arg);

    if (Util::starts_with(arg.full(), "-gdwarf")) {
      // Selection of DWARF format (-gdwarf or -gdwarf-<version>) enables
      // debug info on level 2.
      args_info.generating_debuginfo = true;
      return nullopt;
    }

    if (Util::starts_with(arg.full(), "-gz")) {
      // -gz[=type] neither disables nor enables debug info.
      return nullopt;
    }

    const char last_char = arg.full().back();
    if (last_char == '0') {
      // "-g0", "-ggdb0" or similar: All debug information disabled.
      args_info.generating_debuginfo = false;
      state.generating_debuginfo_level_3 = false;
    } else {
      args_info.generating_debuginfo = true;
      if (last_char == '3') {
        state.generating_debuginfo_level_3 = true;
      }
      if (arg == "-gsplit-dwarf") {
        args_info.seen_split_dwarf = true;
      }
    }
    return nullopt;
  }

  // These options require special handling, because they behave differently
  // with gcc -E, when the output file is not specified.
  if (arg == "-MD" || arg == "-MMD") {
    args_info.generating_dependencies = true;
    args_info.seen_MD_MMD = true;
    state.dep_args.push_back(arg);
    return nullopt;
  }

  if (arg.key() == "-MF") {
    state.dependency_filename_specified = true;
    const auto dep_file = Util::make_relative_path(ctx, arg.value());
    state.dep_args.push_back(Arg(arg.key(),
                                 arg.split_char() == ArgSplit::equal_sign
                                   ? ArgSplit::written_together
                                   : arg.split_char(),
                                 dep_file));
    return nullopt;
  }

  if (arg.key() == "-MQ" || arg.key() == "-MT") {
    ctx.args_info.dependency_target_specified = true;
    const auto relpath = Util::make_relative_path(ctx, arg.value());
    state.dep_args.push_back(Arg(arg.key(), arg.split_char(), relpath));
    return nullopt;
  }

  if (arg == "-fprofile-arcs") {
    args_info.profile_arcs = true;
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (arg == "-ftest-coverage") {
    args_info.generating_coverage = true;
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (arg == "-fstack-usage") {
    args_info.generating_stackusage = true;
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (arg == "--coverage"      // = -fprofile-arcs -ftest-coverage
      || arg == "-coverage") { // Undocumented but still works.
    args_info.profile_arcs = true;
    args_info.generating_coverage = true;
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (Util::starts_with(arg.full(), "-fprofile-")
      || Util::starts_with(arg.full(), "-fauto-profile")
      || arg == "-fbranch-probabilities") {
    if (!process_profiling_option(ctx, arg.full())) {
      // The failure is logged by process_profiling_option.
      return Statistic::unsupported_compiler_option;
    }
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (arg.key() == "-fsanitize-blacklist") {
    args_info.sanitize_blacklists.emplace_back(arg.value());
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (arg.key() == "--sysroot") {
    auto path = arg.value();
    auto relpath = Util::make_relative_path(ctx, path);
    state.common_args.push_back("--sysroot=" + relpath);
    return nullopt;
  }

  // Alternate form of specifying sysroot without =
  if (arg == "--sysroot") {
    if (i == args.size() - 1) {
      log("Missing argument to {}", arg.full());
      return Statistic::bad_compiler_arguments;
    }
    state.common_args.push_back(arg);
    auto relpath = Util::make_relative_path(ctx, args[i + 1]);
    state.common_args.push_back(relpath);
    i++;
    return nullopt;
  }

  // Alternate form of specifying target without =
  if (arg == "-target") {
    if (i == args.size() - 1) {
      log("Missing argument to {}", arg.full());
      return Statistic::bad_compiler_arguments;
    }
    state.common_args.push_back(arg);
    state.common_args.push_back(args[i + 1]);
    i++;
    return nullopt;
  }

  if (Util::starts_with(arg.full(), "-Wp,")) {
    if (arg == "-Wp,-P" || arg.full().find(",-P,") != std::string::npos
        || Util::ends_with(arg.full(), ",-P")) {
      // -P removes preprocessor information in such a way that the object file
      // from compiling the preprocessed file will not be equal to the object
      // file produced when compiling without ccache.
      log("Too hard option -Wp,-P detected");
      return Statistic::unsupported_compiler_option;
    } else if (Util::starts_with(arg.full(), "-Wp,-MD,")
               && arg.full().find(',', 8) == std::string::npos) {
      args_info.generating_dependencies = true;
      state.dependency_filename_specified = true;
      args_info.output_dep =
        Util::make_relative_path(ctx, string_view(arg.full()).substr(8));
      state.dep_args.push_back(arg);
      return nullopt;
    } else if (Util::starts_with(arg.full(), "-Wp,-MMD,")
               && arg.full().find(',', 9) == std::string::npos) {
      args_info.generating_dependencies = true;
      state.dependency_filename_specified = true;
      args_info.output_dep =
        Util::make_relative_path(ctx, string_view(arg.full()).substr(9));
      state.dep_args.push_back(arg);
      return nullopt;
    } else if (Util::starts_with(arg.full(), "-Wp,-D")
               && arg.full().find(',', 6) == std::string::npos) {
      // Treat it like -D.
      state.cpp_args.push_back(arg.full().substr(4));
      return nullopt;
    } else if (arg == "-Wp,-MP"
               || (arg.full().size() > 8
                   && Util::starts_with(arg.full(), "-Wp,-M")
                   && arg.full()[7] == ','
                   && (arg.full()[6] == 'F' || arg.full()[6] == 'Q'
                       || arg.full()[6] == 'T')
                   && arg.full().find(',', 8) == std::string::npos)) {
      // TODO: Make argument to MF/MQ/MT relative.
      state.dep_args.push_back(arg);
      return nullopt;
    } else if (config.direct_mode()) {
      // -Wp, can be used to pass too hard options to the preprocessor.
      // Hence, disable direct mode.
      log("Unsupported compiler option for direct mode: {}", arg.full());
      config.set_direct_mode(false);
    }

    // Any other -Wp,* arguments are only relevant for the preprocessor.
    state.cpp_args.push_back(arg);
    return nullopt;
  }

  if (arg == "-MP") {
    state.dep_args.push_back(arg);
    return nullopt;
  }

  // Input charset needs to be handled specially.
  if (arg.key() == "-finput-charset") {
    state.input_charset_option = std::string(arg.full());
    return nullopt;
  }

  if (arg == "--serialize-diagnostics") {
    if (i == args.size() - 1) {
      log("Missing argument to {}", arg.full());
      return Statistic::bad_compiler_arguments;
    }
    args_info.generating_diagnostics = true;
    args_info.output_dia = Util::make_relative_path(ctx, args[i + 1]);
    i++;
    return nullopt;
  }

  if (arg == "-fcolor-diagnostics" || arg == "-fdiagnostics-color"
      || arg == "-fdiagnostics-color=always") {
    state.color_diagnostics = ColorDiagnostics::always;
    return nullopt;
  }
  if (arg == "-fno-color-diagnostics" || arg == "-fno-diagnostics-color"
      || arg == "-fdiagnostics-color=never") {
    state.color_diagnostics = ColorDiagnostics::never;
    return nullopt;
  }
  if (arg == "-fdiagnostics-color=auto") {
    state.color_diagnostics = ColorDiagnostics::automatic;
    return nullopt;
  }

  // GCC
  if (arg == "-fdirectives-only") {
    state.found_directives_only = true;
    return nullopt;
  }

  // Clang
  if (arg == "-frewrite-includes") {
    state.found_rewrite_includes = true;
    return nullopt;
  }

  if (arg == "-fno-pch-timestamp") {
    args_info.fno_pch_timestamp = true;
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (arg == "-fpch-preprocess") {
    state.found_fpch_preprocess = true;
    state.common_args.push_back(arg);
    return nullopt;
  }

  if (config.sloppiness() & SLOPPY_CLANG_INDEX_STORE
      && arg == "-index-store-path") {
    // Xcode 9 or later calls Clang with this option. The given path includes a
    // UUID that might lead to cache misses, especially when cache is shared
    // among multiple users.
    i++;
    if (i <= args.size() - 1) {
      log("Skipping argument -index-store-path {}", arg.full());
    }
    return nullopt;
  }

  // Options taking an argument that we may want to rewrite to relative paths to
  // get better hit rate. A secondary effect is that paths in the standard error
  // output produced by the compiler will be normalized.
  if (compopt_takes_path(arg.full())) {
    if (i == args.size() - 1) {
      log("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }

    // In the -Xclang -include-(pch/pth) -Xclang <path> case, the path is one
    // index further behind.
    int next = 1;
    if (args[i + 1] == "-Xclang" && i + 2 < args.size()) {
      next = 2;
    }

    if (!detect_pch(
          ctx, args[i], args[i + next], next == 2, &state.found_pch)) {
      return Statistic::bad_compiler_arguments;
    }

    std::string relpath = Util::make_relative_path(ctx, args[i + next]);
    auto& dest_args =
      compopt_affects_cpp(args[i]) ? state.cpp_args : state.common_args;
    dest_args.push_back(args[i]);
    if (next == 2) {
      dest_args.push_back(args[i + 1]);
    }
    dest_args.push_back(relpath);

    i += next;
    return nullopt;
  }

  // Same as above but options with concatenated argument beginning with a
  // slash.
  if (args[i].full()[0] == '-') {
    size_t slash_pos = args[i].full().find('/');
    if (slash_pos != std::string::npos) {
      std::string option = args[i].full().substr(0, slash_pos);
      if (compopt_takes_concat_arg(option) && compopt_takes_path(option)) {
        auto relpath =
          Util::make_relative_path(ctx, string_view(args[i]).substr(slash_pos));
        std::string new_option = option + relpath;
        if (compopt_affects_cpp(option)) {
          state.cpp_args.push_back(new_option);
        } else {
          state.common_args.push_back(new_option);
        }
        return nullopt;
      }
    }
  }

  // Options that take an argument.
  if (compopt_takes_arg(args[i])) {
    if (i == args.size() - 1) {
      log("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }

    if (compopt_affects_cpp(args[i])) {
      state.cpp_args.push_back(args[i]);
      state.cpp_args.push_back(args[i + 1]);
    } else {
      state.common_args.push_back(args[i]);
      state.common_args.push_back(args[i + 1]);
    }

    i++;
    return nullopt;
  }

  // Other options.
  if (args[i].full()[0] == '-') {
    if (compopt_affects_cpp(args[i]) || compopt_prefix_affects_cpp(args[i])) {
      state.cpp_args.push_back(args[i]);
    } else {
      state.common_args.push_back(args[i]);
    }
    return nullopt;
  }

  // If an argument isn't a plain file then assume its an option, not an input
  // file. This allows us to cope better with unusual compiler options.
  //
  // Note that "/dev/null" is an exception that is sometimes used as an input
  // file when code is testing compiler flags.
  if (args[i] != "/dev/null") {
    auto st = Stat::stat(args[i]);
    if (!st || !st.is_regular()) {
      log("{} is not a regular file, not considering as input file", args[i]);
      state.common_args.push_back(args[i]);
      return nullopt;
    }
  }

  if (!args_info.input_file.empty()) {
    if (!language_for_file(args[i]).empty()) {
      log("Multiple input files: {} and {}", args_info.input_file, args[i]);
      return Statistic::multiple_source_files;
    } else if (!state.found_c_opt && !state.found_dc_opt) {
      log("Called for link with {}", args[i]);
      if (args[i].full().find("conftest.") != std::string::npos) {
        return Statistic::autoconf_test;
      } else {
        return Statistic::called_for_link;
      }
    } else {
      log("Unsupported source extension: {}", args[i]);
      return Statistic::unsupported_source_language;
    }
  }

  // The source code file path gets put into the notes.
  if (args_info.generating_coverage) {
    args_info.input_file = args[i];
    return nullopt;
  }

  // Rewrite to relative to increase hit rate.
  args_info.input_file = Util::make_relative_path(ctx, args[i]);

  return nullopt;
}

void
handle_dependency_environment_variables(Context& ctx,
                                        ArgumentProcessingState& state)
{
  ArgsInfo& args_info = ctx.args_info;

  // See <http://gcc.gnu.org/onlinedocs/cpp/Environment-Variables.html>.
  // Contrary to what the documentation seems to imply the compiler still
  // creates object files with these defined (confirmed with GCC 8.2.1), i.e.
  // they work as -MMD/-MD, not -MM/-M. These environment variables do nothing
  // on Clang.
  const char* dependencies_env = getenv("DEPENDENCIES_OUTPUT");
  bool using_sunpro_dependencies = false;
  if (!dependencies_env) {
    dependencies_env = getenv("SUNPRO_DEPENDENCIES");
    using_sunpro_dependencies = true;
  }
  if (!dependencies_env) {
    return;
  }

  args_info.generating_dependencies = true;
  state.dependency_filename_specified = true;

  auto dependencies = Util::split_into_views(dependencies_env, " ");

  if (!dependencies.empty()) {
    auto abspath_file = dependencies[0];
    args_info.output_dep = Util::make_relative_path(ctx, abspath_file);
  }

  // Specifying target object is optional.
  if (dependencies.size() > 1) {
    // It's the "file target" form.
    ctx.args_info.dependency_target_specified = true;
    string_view abspath_obj = dependencies[1];
    std::string relpath_obj = Util::make_relative_path(ctx, abspath_obj);
    // Ensure that the compiler gets a relative path.
    std::string relpath_both =
      fmt::format("{} {}", args_info.output_dep, relpath_obj);
    if (using_sunpro_dependencies) {
      Util::setenv("SUNPRO_DEPENDENCIES", relpath_both);
    } else {
      Util::setenv("DEPENDENCIES_OUTPUT", relpath_both);
    }
  } else {
    // It's the "file" form.
    state.dependency_implicit_target_specified = true;
    // Ensure that the compiler gets a relative path.
    if (using_sunpro_dependencies) {
      Util::setenv("SUNPRO_DEPENDENCIES", args_info.output_dep);
    } else {
      Util::setenv("DEPENDENCIES_OUTPUT", args_info.output_dep);
    }
  }
}

} // namespace

ProcessArgsResult
process_args(Context& ctx)
{
  ASSERT(!ctx.orig_args.empty());

  ArgsInfo& args_info = ctx.args_info;
  Config& config = ctx.config;

  // args is a copy of the original arguments given to the compiler but with
  // arguments from @file and similar constructs expanded. It's only used as a
  // temporary data structure to loop over.
  Args args = ctx.orig_args;
  args.add_param("--ccache-skip", {ArgSplit::space});
  args.add_param("-optf", {ArgSplit::space});
  args.add_param("--options-file", {ArgSplit::space});
  args.add_param("-arch", {ArgSplit::space});
  args.add_param("-x", {ArgSplit::space, ArgSplit::written_together});
  args.add_param(
    "-MF", {ArgSplit::space, ArgSplit::equal_sign, ArgSplit::written_together});
  args.add_param("-MQ", {ArgSplit::space, ArgSplit::written_together});
  args.add_param("-MT", {ArgSplit::space, ArgSplit::written_together});

  ArgumentProcessingState state;

  state.common_args.push_back(args[0]); // Compiler

  for (size_t i = 1; i < args.size(); i++) {
    auto error = process_arg(ctx, args, i, state);
    if (error) {
      return *error;
    }
  }

  if (state.generating_debuginfo_level_3 && !config.run_second_cpp()) {
    log("Generating debug info level 3; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  handle_dependency_environment_variables(ctx, state);

  if (args_info.input_file.empty()) {
    log("No input file found");
    return Statistic::no_input_file;
  }

  if (state.found_pch || state.found_fpch_preprocess) {
    args_info.using_precompiled_header = true;
    if (!(config.sloppiness() & SLOPPY_TIME_MACROS)) {
      log(
        "You have to specify \"time_macros\" sloppiness when using"
        " precompiled headers to get direct hits");
      log("Disabling direct mode");
      return Statistic::could_not_use_precompiled_header;
    }
  }

  if (args_info.profile_path.empty()) {
    args_info.profile_path = ctx.apparent_cwd;
  }

  if (!state.explicit_language.empty() && state.explicit_language == "none") {
    state.explicit_language.clear();
  }
  state.file_language = language_for_file(args_info.input_file);
  if (!state.explicit_language.empty()) {
    if (!language_is_supported(state.explicit_language)) {
      log("Unsupported language: {}", state.explicit_language);
      return Statistic::unsupported_source_language;
    }
    args_info.actual_language = state.explicit_language;
  } else {
    args_info.actual_language = state.file_language;
  }

  args_info.output_is_precompiled_header =
    args_info.actual_language.find("-header") != std::string::npos
    || Util::is_precompiled_header(args_info.output_obj);

  if (args_info.output_is_precompiled_header
      && !(config.sloppiness() & SLOPPY_PCH_DEFINES)) {
    log(
      "You have to specify \"pch_defines,time_macros\" sloppiness when"
      " creating precompiled headers");
    return Statistic::could_not_use_precompiled_header;
  }

  if (!state.found_c_opt && !state.found_dc_opt && !state.found_S_opt) {
    if (args_info.output_is_precompiled_header) {
      state.common_args.push_back("-c");
    } else {
      log("No -c option found");
      // Having a separate statistic for autoconf tests is useful, as they are
      // the dominant form of "called for link" in many cases.
      return args_info.input_file.find("conftest.") != std::string::npos
               ? Statistic::autoconf_test
               : Statistic::called_for_link;
    }
  }

  if (args_info.actual_language.empty()) {
    log("Unsupported source extension: {}", args_info.input_file);
    return Statistic::unsupported_source_language;
  }

  if (!config.run_second_cpp() && args_info.actual_language == "cu") {
    log("Using CUDA compiler; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  args_info.direct_i_file = language_is_preprocessed(args_info.actual_language);

  if (args_info.output_is_precompiled_header && !config.run_second_cpp()) {
    // It doesn't work to create the .gch from preprocessed source.
    log("Creating precompiled header; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  if (config.cpp_extension().empty()) {
    std::string p_language = p_language_for_language(args_info.actual_language);
    config.set_cpp_extension(extension_for_language(p_language).substr(1));
  }

  // Don't try to second guess the compilers heuristics for stdout handling.
  if (args_info.output_obj == "-") {
    log("Output file is -");
    return Statistic::output_to_stdout;
  }

  if (args_info.output_obj.empty()) {
    if (args_info.output_is_precompiled_header) {
      args_info.output_obj = args_info.input_file + ".gch";
    } else {
      string_view extension = state.found_S_opt ? ".s" : ".o";
      args_info.output_obj = Util::change_extension(
        Util::base_name(args_info.input_file), extension);
    }
  }

  if (args_info.seen_split_dwarf) {
    size_t pos = args_info.output_obj.rfind('.');
    if (pos == std::string::npos || pos == args_info.output_obj.size() - 1) {
      log("Badly formed object filename");
      return Statistic::bad_compiler_arguments;
    }

    args_info.output_dwo = Util::change_extension(args_info.output_obj, ".dwo");
  }

  // Cope with -o /dev/null.
  if (args_info.output_obj != "/dev/null") {
    auto st = Stat::stat(args_info.output_obj);
    if (st && !st.is_regular()) {
      log("Not a regular file: {}", args_info.output_obj);
      return Statistic::bad_output_file;
    }
  }

  auto output_dir = std::string(Util::dir_name(args_info.output_obj));
  auto st = Stat::stat(output_dir);
  if (!st || !st.is_directory()) {
    log("Directory does not exist: {}", output_dir);
    return Statistic::bad_output_file;
  }

  // Some options shouldn't be passed to the real compiler when it compiles
  // preprocessed code:
  //
  // -finput-charset=XXX (otherwise conversion happens twice)
  // -x XXX (otherwise the wrong language is selected)
  if (!state.input_charset_option.empty()) {
    state.cpp_args.push_back(state.input_charset_option);
  }
  if (state.found_pch) {
    state.cpp_args.push_back("-fpch-preprocess");
  }
  if (!state.explicit_language.empty()) {
    state.cpp_args.push_back("-x");
    state.cpp_args.push_back(state.explicit_language);
  }

  args_info.strip_diagnostics_colors =
    state.color_diagnostics != ColorDiagnostics::automatic
      ? state.color_diagnostics == ColorDiagnostics::never
      : !color_output_possible();
  // Since output is redirected, compilers will not color their output by
  // default, so force it explicitly.
  if (ctx.guessed_compiler == GuessedCompiler::clang) {
    if (args_info.actual_language != "assembler") {
      if (!config.run_second_cpp()) {
        state.cpp_args.push_back("-fcolor-diagnostics");
      }
      state.compiler_only_args.push_back("-fcolor-diagnostics");
      add_depend_mode_extra_original_args(ctx, "-fcolor-diagnostics");
    }
  } else if (ctx.guessed_compiler == GuessedCompiler::gcc) {
    if (!config.run_second_cpp()) {
      state.cpp_args.push_back("-fdiagnostics-color");
    }
    state.compiler_only_args.push_back("-fdiagnostics-color");
    add_depend_mode_extra_original_args(ctx, "-fdiagnostics-color");
  } else {
    // Other compilers shouldn't output color, so no need to strip it.
    args_info.strip_diagnostics_colors = false;
  }

  if (args_info.generating_dependencies) {
    if (!state.dependency_filename_specified) {
      auto default_depfile_name =
        Util::change_extension(args_info.output_obj, ".d");
      args_info.output_dep =
        Util::make_relative_path(ctx, default_depfile_name);
      if (!config.run_second_cpp()) {
        // If we're compiling preprocessed code we're sending dep_args to the
        // preprocessor so we need to use -MF to write to the correct .d file
        // location since the preprocessor doesn't know the final object path.
        state.dep_args.push_back("-MF");
        state.dep_args.push_back(default_depfile_name);
      }
    }

    if (!ctx.args_info.dependency_target_specified
        && !state.dependency_implicit_target_specified
        && !config.run_second_cpp()) {
      // If we're compiling preprocessed code we're sending dep_args to the
      // preprocessor so we need to use -MQ to get the correct target object
      // file in the .d file.
      state.dep_args.push_back("-MQ");
      state.dep_args.push_back(args_info.output_obj);
    }
  }

  if (args_info.generating_coverage) {
    args_info.output_cov = Util::make_relative_path(
      ctx, Util::change_extension(args_info.output_obj, ".gcno"));
  }

  if (args_info.generating_stackusage) {
    auto default_sufile_name =
      Util::change_extension(args_info.output_obj, ".su");
    args_info.output_su = Util::make_relative_path(ctx, default_sufile_name);
  }

  Args compiler_args = state.common_args;
  compiler_args.push_back(state.compiler_only_args);

  if (config.run_second_cpp()) {
    compiler_args.push_back(state.cpp_args);
  } else if (state.found_directives_only || state.found_rewrite_includes) {
    // Need to pass the macros and any other preprocessor directives again.
    compiler_args.push_back(state.cpp_args);
    if (state.found_directives_only) {
      state.cpp_args.push_back("-fdirectives-only");
      // The preprocessed source code still needs some more preprocessing.
      compiler_args.push_back("-fpreprocessed");
      compiler_args.push_back("-fdirectives-only");
    }
    if (state.found_rewrite_includes) {
      state.cpp_args.push_back("-frewrite-includes");
      // The preprocessed source code still needs some more preprocessing.
      compiler_args.push_back("-x");
      compiler_args.push_back(args_info.actual_language);
    }
  } else if (!state.explicit_language.empty()) {
    // Workaround for a bug in Apple's patched distcc -- it doesn't properly
    // reset the language specified with -x, so if -x is given, we have to
    // specify the preprocessed language explicitly.
    compiler_args.push_back("-x");
    compiler_args.push_back(p_language_for_language(state.explicit_language));
  }

  if (state.found_c_opt) {
    compiler_args.push_back("-c");
  }

  if (state.found_dc_opt) {
    compiler_args.push_back("-dc");
  }

  for (const auto& arch : args_info.arch_args) {
    compiler_args.push_back("-arch");
    compiler_args.push_back(arch);
  }

  Args preprocessor_args = state.common_args;
  preprocessor_args.push_back(state.cpp_args);

  if (config.run_second_cpp()) {
    // When not compiling the preprocessed source code, only pass dependency
    // arguments to the compiler to avoid having to add -MQ, supporting e.g.
    // EDG-based compilers which don't support -MQ.
    compiler_args.push_back(state.dep_args);
  } else {
    // When compiling the preprocessed source code, pass dependency arguments to
    // the preprocessor since the compiler doesn't produce a .d file when
    // compiling preprocessed source code.
    preprocessor_args.push_back(state.dep_args);
  }

  Args extra_args_to_hash = state.compiler_only_args;
  if (config.run_second_cpp()) {
    extra_args_to_hash.push_back(state.dep_args);
  }

  return {preprocessor_args, extra_args_to_hash, compiler_args};
}
