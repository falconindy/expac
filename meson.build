project('expac', 'c',
        version : '10',
        license : 'MIT',
        default_options : [
          'c_std=c11',
          'prefix=/usr',
        ])

libalpm = dependency('libalpm')

conf = configuration_data()
conf.set('_GNU_SOURCE', true)
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())

cc = meson.get_compiler('c')

configure_file(
    output : 'config.h',
    configuration : conf)
add_project_arguments('-include', 'config.h', language : 'c')

expac_exe = executable(
  'expac',
  files('''
    src/expac.c
    src/conf.c src/conf.h
    src/util.h
  '''.split()),
  dependencies : [
    libalpm,
  ],
  install : true)

python = import('python')
py3 = python.find_installation('python3', required : false)
if py3.found()
  foreach t : [
    'test_query.py',
    'test_sync.py',
    'test_file.py',
    'test_format.py',
    'test_options.py',
    'test_targets.py',
  ]
    test(
      t.split('.')[0],
      py3,
      args : [meson.project_source_root() / 'tests' / t],
      depends : [expac_exe],
      env : ['PYTHONDONTWRITEBYTECODE=1'],
      suite : 'integration')
  endforeach
endif

pod2man = find_program('pod2man')
man = custom_target(
    'man',
    output : 'expac.1',
    input : 'README.pod',
    command : [
      pod2man,
      '--section=1',
      '--center=expac Manual',
      '--name=EXPAC',
      '--release=expac @0@'.format(meson.project_version()),
      '@INPUT@', '@OUTPUT@'
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1'))
