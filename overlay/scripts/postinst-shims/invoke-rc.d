#!/bin/sh
# postinst-run shim: invoke-rc.d [options] <service> <action>
while [ "${1#--}" != "$1" ]; do shift; done
SERVICE=$1
ACTION=$2
case "$ACTION" in
	start|stop|restart|reload|force-reload|try-restart) exec systemctl --no-block "$ACTION" "$SERVICE.service" ;;
	*) exit 0 ;;
esac
