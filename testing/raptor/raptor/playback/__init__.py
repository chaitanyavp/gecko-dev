from __future__ import absolute_import

from mozlog import get_proxy_logger
from .mitmproxy import MitmproxyDesktop, MitmproxyAndroid

LOG = get_proxy_logger(component='mitmproxy')

playback_cls = {
    'mitmproxy': MitmproxyDesktop,
    'mitmproxy-android': MitmproxyAndroid,
}


def get_playback(config, android_device=None):
    tool_name = config.get('playback_tool', None)
    if tool_name is None:
        LOG.critical("playback_tool name not found in config")
        return
    if playback_cls.get(tool_name, None) is None:
        LOG.critical("specified playback tool is unsupported: %s" % tool_name)
        return None

    cls = playback_cls.get(tool_name)
    if android_device is None:
        return cls(config)
    else:
        return cls(config, android_device)
