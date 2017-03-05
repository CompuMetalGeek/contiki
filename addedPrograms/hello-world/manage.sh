# stop when an error occurs
set -e

# defaults
defaultParams="cru";

# read parameters
for var in "$@";
do
	if [[ $var = -* ]];
		then params="$params${var:1}";
	fi;
	if [[ $var = target\=* ]];
		then target=${var:7};
	fi;
	if [[ $var = build\=* ]];
		then build=${var:6};
	fi;
done;

# apply defaults if neccesary
params=${params:-$defaultParams};

if [[ $params =~ "c" ]];
	then echo "--- cleaning project ---";
	sudo make TARGET=zoul BUILD=remote-reva hello-world clean;
	echo "--- done. ---";
fi;
if [[ $params =~ "m" ]];
	then echo "--- making project ---";
	sudo make TARGET=zoul BUILD=remote-reva hello-world;
	echo "--- done. ---";
fi;
if [[ $params =~ "u" ]];
	then echo "--- uploading project ---";
	sudo make TARGET=zoul BUILD=remote-reva hello-world.upload;
	echo "--- done. ---";
fi;
if [[ $params =~ "r" ]];
	then echo "--- reading from device \(via putty\) ---";
	sudo putty -load re-mote;
fi;