## Manual building example

### Prerequisites
 * [hl2sdk](https://github.com/alliedmodders/hl2sdk) of games you plan on writing plugin for (this sample only supports Source2 based games);
 * [hl2sdk-manifests](https://github.com/alliedmodders/hl2sdk-manifests);
 * [metamod-source](https://github.com/alliedmodders/metamod-source);
 * [python3](https://www.python.org/)
 * [ambuild](https://github.com/alliedmodders/ambuild), make sure ``ambuild`` command is available via the ``PATH`` environment variable;

### Info
 Simple Chat Filter plugin for windows servers
 Configuration must be placed at addons\cs2_chat_filter_plugin\config\chat_filter_config.ini and saved in utf8 format
 use sv_chat_filter_mode ConVar to control how this plguin works:
	0 filter disabled
	1 filter replace match for everybody
	2 filter replace match for everybody except sender
