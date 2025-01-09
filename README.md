# tree
- [what is tree](https://en.m.wikipedia.org/wiki/Tree_(command))


- This module is just a bash script that attempts to mimic the behavior of the Linux tree command. This is not a compiled `BINARY` and it is slower than the original tree command also many of the  original `tree` arguments are not supported

###### Requirements
- bash

###### Arguments which *currently are supported
```
tree -a | #ShowsHiddenFiles
tree -n | #NoColor
tree -d | #ShowDirectoriesOnly
tree -L | #LimitsTheDepthOfTree
tree -i | #UsesASCII
```

# Comparison

- LinuxTree Binary Output 

```
tree /storage/emulated/0/tree
/storage/emulated/0/tree
├── META-INF
│   └── com
│       └── google
│           └── android
│               ├── update-binary
│               └── updater-script
├── customize.sh
├── module.prop
├── system
│   └── bin
│       └── tree
└── tree.zip

7 directories, 6 files
```
###### This module's tree shell script output 
```
./tree.sh /storage/emulated/0/tree
/storage/emulated/0/tree
├── META-INF
│   └── com
│       └── google
│           └── android
│               ├── update-binary
│               └── updater-script
├── customize.sh
├── module.prop
├── system
│   └── bin
│       └── tree
└── tree.zip

6 directories, 6 files

```

#### Bugs
- ~~Sometimes , it fails while listing big directories~~


###### Compiled (bash) binaries source
- https://github.com/Magisk-Modules-Alt-Repo/mkshrc/tree/master/common/bash
