from parts import *

#enable smart linking
SetOptionDefault("LINKFLAGS", ['-Wl,--copy-dt-needed-entries', '-Wl,--as-needed'])
SetOptionDefault("CXXFLAGS", ['-std=c++17'])

# control shim for trafficserver
AddOption("--with-trafficserver",
          dest='with_trafficserver',
          nargs=1,
          type='string',
          action='store',
          metavar='DIR',
          default=None,
          help='Optional path to custom buld of trafficserver')

path = GetOption("with_trafficserver")

Part("plugin/txn_box.part", package_group='txn_box')

# the depends
Part("#lib/libyaml.part",vcs_type=VcsGit(server="github.com", repository="jbeder/yaml-cpp.git", tag="yaml-cpp-0.6.2"))
Part("swoc++/swoc++.part",vcs_type=VcsGit(server="github.com", repository="SolidWallOfCode/libswoc", tag="dev-1-0-8"))
# this is just a shim part.. it only passes info based on stuff being install on the box
# it should have a better check for the real version of trafficserver being used
Part("#lib/trafficserver.part",CUSTOM_PATH=path,VERSION="7.0.0"),


