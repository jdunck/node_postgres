import os 

srcdir = '.'
blddir = 'build'
VERSION = '0.0.1'

def set_options(opt):
  opt.tool_options('compiler_cxx')

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('node_addon')
  conf.check_node_headers()

  pg_config = conf.find_program('pg_config', var='PG_CONFIG', mandatory=True)
  pg_libdir = os.popen("%s --libdir" % pg_config).readline().strip()
  conf.env.append_value("LIBPATH_PG", pg_libdir)
  conf.env.append_value("LIB_PG", "pq")
  pg_includedir = os.popen("%s --includedir" % pg_config).readline().strip()
  conf.env.append_value("CPPPATH_PG", pg_includedir)

def build(bld):
  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.target = 'binding'
  obj.source = "binding.cc"
  obj.uselib = "PG"


def shutdown():
  # HACK to get binding.node out of build directory.
  # better way to do this?
  import Options, shutil
  if not Options.commands['clean']:
    if os.path.exists('build/default/binding.node'):
      shutil.copy('build/default/binding.node', 'binding.node')
  else:
    if os.path.exists('binding.node'):
      os.unlink('binding.node')
