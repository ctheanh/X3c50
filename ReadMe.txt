cd $BUILDSPACE/
source ./build/envsetup.sh
lunch X3c70-user

make update-api
make -j24

kernel
======
make bootimg

======
Contact Lenovo support for information on obtaining Open Source code.
