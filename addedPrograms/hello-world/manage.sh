# stop when an error occurs
set -e

# defaults
defaultParams="cmru";
defaultProject="hello-world";
defaultTarget="zoul";
defaultBoard="remote-reva";

# read parameters
for var in "$@";
do
	if [[ $var = -* ]];
		then params="$params${var:1}";
	elif [[ $var = target\=* ]];
		then target=${var:7};
	elif [[ $var = build\=* ]];
		then build=${var:6};
	else
		project=$var;
	fi;
done;

# apply defaults if neccesary
board=${board:-$defaultBoard};
params=${params:-$defaultParams};
project=${project:-$defaultProject};
target=${target:-$defaultTarget};
arguments="";

if [[ $params =~ "c" ]];
	then echo "--- clean ---";
	arguments="$arguments clean";
fi;
if [[ $params =~ "m" ]];
	then echo "--- make ---";
	arguments="$arguments $project";
fi;
if [[ $params =~ "u" ]];
	then echo "--- upload ---";
	arguments="${arguments}.upload";
fi;
if [[ $params =~ "r" ]];
	then echo "--- read ---";
	arguments="$arguments login";
fi;

sudo make TARGET=$target BOARD=$board $arguments;