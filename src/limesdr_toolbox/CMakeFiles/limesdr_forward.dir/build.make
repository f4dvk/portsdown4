# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.7

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/pi/rpidatv/src/limesdr_toolbox

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/pi/rpidatv/src/limesdr_toolbox

# Include any dependencies generated for this target.
include CMakeFiles/limesdr_forward.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/limesdr_forward.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/limesdr_forward.dir/flags.make

CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o: CMakeFiles/limesdr_forward.dir/flags.make
CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o: limesdr_forward.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/pi/rpidatv/src/limesdr_toolbox/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o   -c /home/pi/rpidatv/src/limesdr_toolbox/limesdr_forward.c

CMakeFiles/limesdr_forward.dir/limesdr_forward.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/limesdr_forward.dir/limesdr_forward.c.i"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/pi/rpidatv/src/limesdr_toolbox/limesdr_forward.c > CMakeFiles/limesdr_forward.dir/limesdr_forward.c.i

CMakeFiles/limesdr_forward.dir/limesdr_forward.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/limesdr_forward.dir/limesdr_forward.c.s"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/pi/rpidatv/src/limesdr_toolbox/limesdr_forward.c -o CMakeFiles/limesdr_forward.dir/limesdr_forward.c.s

CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.requires:

.PHONY : CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.requires

CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.provides: CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.requires
	$(MAKE) -f CMakeFiles/limesdr_forward.dir/build.make CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.provides.build
.PHONY : CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.provides

CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.provides.build: CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o


CMakeFiles/limesdr_forward.dir/limesdr_util.c.o: CMakeFiles/limesdr_forward.dir/flags.make
CMakeFiles/limesdr_forward.dir/limesdr_util.c.o: limesdr_util.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/pi/rpidatv/src/limesdr_toolbox/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building C object CMakeFiles/limesdr_forward.dir/limesdr_util.c.o"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/limesdr_forward.dir/limesdr_util.c.o   -c /home/pi/rpidatv/src/limesdr_toolbox/limesdr_util.c

CMakeFiles/limesdr_forward.dir/limesdr_util.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/limesdr_forward.dir/limesdr_util.c.i"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/pi/rpidatv/src/limesdr_toolbox/limesdr_util.c > CMakeFiles/limesdr_forward.dir/limesdr_util.c.i

CMakeFiles/limesdr_forward.dir/limesdr_util.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/limesdr_forward.dir/limesdr_util.c.s"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/pi/rpidatv/src/limesdr_toolbox/limesdr_util.c -o CMakeFiles/limesdr_forward.dir/limesdr_util.c.s

CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.requires:

.PHONY : CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.requires

CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.provides: CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.requires
	$(MAKE) -f CMakeFiles/limesdr_forward.dir/build.make CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.provides.build
.PHONY : CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.provides

CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.provides.build: CMakeFiles/limesdr_forward.dir/limesdr_util.c.o


# Object files for target limesdr_forward
limesdr_forward_OBJECTS = \
"CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o" \
"CMakeFiles/limesdr_forward.dir/limesdr_util.c.o"

# External object files for target limesdr_forward
limesdr_forward_EXTERNAL_OBJECTS =

limesdr_forward: CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o
limesdr_forward: CMakeFiles/limesdr_forward.dir/limesdr_util.c.o
limesdr_forward: CMakeFiles/limesdr_forward.dir/build.make
limesdr_forward: /usr/local/lib/libLimeSuite.so
limesdr_forward: CMakeFiles/limesdr_forward.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/pi/rpidatv/src/limesdr_toolbox/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Linking C executable limesdr_forward"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/limesdr_forward.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/limesdr_forward.dir/build: limesdr_forward

.PHONY : CMakeFiles/limesdr_forward.dir/build

CMakeFiles/limesdr_forward.dir/requires: CMakeFiles/limesdr_forward.dir/limesdr_forward.c.o.requires
CMakeFiles/limesdr_forward.dir/requires: CMakeFiles/limesdr_forward.dir/limesdr_util.c.o.requires

.PHONY : CMakeFiles/limesdr_forward.dir/requires

CMakeFiles/limesdr_forward.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/limesdr_forward.dir/cmake_clean.cmake
.PHONY : CMakeFiles/limesdr_forward.dir/clean

CMakeFiles/limesdr_forward.dir/depend:
	cd /home/pi/rpidatv/src/limesdr_toolbox && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/pi/rpidatv/src/limesdr_toolbox /home/pi/rpidatv/src/limesdr_toolbox /home/pi/rpidatv/src/limesdr_toolbox /home/pi/rpidatv/src/limesdr_toolbox /home/pi/rpidatv/src/limesdr_toolbox/CMakeFiles/limesdr_forward.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/limesdr_forward.dir/depend
