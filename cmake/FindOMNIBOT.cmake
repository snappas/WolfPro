# - Find OMNIBOT

find_path(OMNIBOT_INCLUDE_DIR Omni-Bot.h
	${PROJECT_SOURCE_DIR}/../omni-bot/0.83/Omnibot/Common
	DOC "The directory where OMNIBOT.h resides"
)

# handle the QUIETLY and REQUIRED arguments and set CURL_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OMNIBOT
	REQUIRED_VARS OMNIBOT_INCLUDE_DIR)

