project('expac', 'c',
        version : '9',
        license : 'MIT',
        default_options : [
          'c_std=c11',
          'prefix=/usr',
        ])

conf = configuration_data()
conf.set('_GNU_SOURCE', true)
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())

configure_file(
    output : 'config.h',
    configuration : conf)
add_project_arguments('-include', 'config.h', language : 'c')

executable(
  'expac',
  files('''
	src/expac.c
	src/conf.c src/conf.h
	src/util.h
  '''.split()),
  dependencies : [
    dependency('libalpm')
  ],
  install : true)

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
