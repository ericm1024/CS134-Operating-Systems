#!/bin/bash
#
# sanity check script for Project 1A
#
LAB="lab1a"
README="README"
MAKEFILE="Makefile"

EXPECTED=""
EXPECTEDS="c"
PGM="./lab1a"
PGMS="$PGM"

TIMEOUT=1

if [ -z "$1" ]
then
	echo usage: $0 your-student-id
	exit 1
else
	student=$1
fi

# make sure the tarball has the right name
tarball="$LAB-$student.tar.gz"
if [ ! -s $tarball ]
then
	echo "ERROR: Unable to find submission tarball:" $tarball
	exit 1
fi

# make sure we can untar it
TEMP="/tmp/TestTemp.$$"
echo "... Using temporary testing directory" $TEMP
function cleanup {
	cd
	rm -rf $TEMP
	exit $1
}

mkdir $TEMP
cp $tarball $TEMP
cd $TEMP
echo "... untaring" $tarbsll
tar xvf $tarball
if [ $? -ne 0 ]
then
	echo "ERROR: Error untarring $tarball"
	cleanup 1
fi

# make sure we find all the expected files
echo "... checking for expected files"
for i in $README $MAKEFILE $EXPECTED
do
	if [ ! -s $i ]
	then
		echo "ERROR: unable to find file" $i
		cleanup 1
	else
		echo "        $i ... OK"
	fi
done

# make sure the README contains name and e-mail
echo "... checking for NAME and EMAIL"
name=`grep "NAME:" $README | cut -d: -f2`
if [ -z "$name" ]
then
	echo "ERROR: $README does not contain a NAME: line"
	cleanup 1
else
	echo "Make sure that $name is your correct name."
fi

email=`grep "EMAIL:" $README | cut -d: -f2`
if [ -z "$email" ]
then
	echo "ERROR: $README does not contain a EMAIL: line"
	cleanup 1
else
	echo "Make sure that $email is your correct email address."
fi

# make sure we find files with all the expected suffixes
echo "... checking for files of expected types"
for s in $EXPECTEDS
do
	names=`echo *.$s`
	if [ "$names" = '*'.$s ]
	then
		echo "ERROR: unable to find any .$s files"
		cleanup 1
	else
		for f in $names
		do
			echo "        $f ... OK"
		done
	fi
done

# make sure we can build the expected program
echo "... building default target(s)"
make 2> STDERR
if [ $? -ne 0 ]
then
	echo "ERROR: default make fails"
	cleanup 1
fi
if [ -s STDERR ]
then
	echo "ERROR: make produced output to stderr"
	cleanup 1
fi

echo "... checking for expected products"
for p in $PGMS
do
	if [ ! -x $p ]
	then
		echo "ERROR: unable to find expected executable" $p
		cleanup 1
	else
		echo "        $p ... OK"
	fi
done

# see if it excepts the expected arguments
function testrc {
	if [ $1 -ne $2 ]
	then
		echo "ERROR: expected RC=$2, GOT $1"
		cleanup 1
	fi
}

echo "... does not recognize bogus argument"
timeout $TIMEOUT $PGM --bogus < /dev/tty > /dev/null
testrc $? 1



# looks good
echo "This submission passes the sanity check."
cleanup 0
