#!/bin/bash
source ../Framework/common.sh;
cd $REPO_DIR/jde/TwsWebSocket;
moveToDir install;
moveToDir ProgramData;moveToDir jde-cpp;moveToDir TwsWebSocket;moveToDir blockly;moveToDir build;moveToDir include;
set -e;
function linkDir
{
	destination=$1;
	source=$2/$1;
	local -n _files=$3;
	moveToDir $destination;
	for file in "${_files[@]}"; do
		if [[ $file != *.* ]]; then file=$file.h; fi;
		if [ ! -f $file ]; then mklink $file $source; fi;
	done
}
function linkRecursive
{
	destination=$1
	local source=$2/$destination
	#echo $source;
	moveToDir $destination;
	for filename in $source/*; do
		base=$(basename "$filename");
		#echo $filename;
		if [ -d $filename ]; then
			echo linkRecursive $base $source;
			#_source=$source; #only 1 level...
			linkRecursive $base $source;
			#source=_source;
			echo pwd=`pwd`;
		else
			#echo mklink $base $source;
			mklink $base $source;
		fi;
	done;
	cd ..;
}

declare -a files=();
#files=();
#echo $REPO_BASH;
for filename in $REPO_BASH/vcpkg/installed/x64-windows/include/fmt/*; do files+=( $(basename "$filename") ); done;
linkDir fmt $REPO_BASH/vcpkg/installed/x64-windows/include files;

files=( "any" "arena" "arena_impl" "arenastring" "descriptor" "extension_set" "generated_enum_reflection" "generated_enum_util" "generated_message_reflection" "generated_message_table_driven" "generated_message_util" "has_bits" "implicit_weak_message" "map" "map_entry" "map_entry_lite" "map_field" "map_field_inl" "map_field_lite" "map_type_handler" "message" "message_lite" "metadata_lite" "parse_context" "port" "port_def.inc" "port_undef.inc" "reflection_ops" "repeated_field" "unknown_field_set" "wire_format_lite" );
cd ..;moveToDir google; linkDir protobuf $REPO_BASH/protobuf/src/google files;

files=( "coded_stream" "zero_copy_stream" "zero_copy_stream_impl_lite" );
linkDir io $REPO_BASH/protobuf/src/google/protobuf files;
files=( "callback" "casts" "common" "hash" "logging" "macros" "mutex" "once" "platform_macros" "port" "stl_util" "stringpiece" "strutil" );
cd ..;linkDir stubs $REPO_BASH/protobuf/src/google/protobuf files
cd ../../..;linkRecursive spdlog $REPO_BASH/vcpkg/installed/x64-windows/include;


