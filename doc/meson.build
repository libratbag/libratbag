if get_option('documentation')

cmd_sphinx = find_program('sphinx-build-3', 'sphinx-build3', 'sphinx-build')

doc_config = configuration_data()
doc_config.set('PACKAGE_NAME', meson.project_name())
doc_config.set('PACKAGE_VERSION', meson.project_version())

# Sphinx really really wants a _static directory in its source tree. Let's
# make it happy
run_command('mkdir', '-p', join_paths(project_build_root, 'doc', '_static'))

# Effectively a noop copy. We *must* generate enums.rst so that will end up
# in the build directory. sphinx doesn't allow for multiple source
# directories, so any .rst and the conf.py must be copied over to the
# build directory too
src_rsts = ['index.rst', 'dbus.rst']
dst_rsts = []
foreach rst : src_rsts
	f = configure_file(input : rst,
			   output : rst,
			   configuration : doc_config,
			   install : false)
	dst_rsts += f
endforeach

sphinx_conf = configure_file(input : 'conf.py.in',
			     output : 'conf.py',
			     configuration : doc_config,
			     install : false)

custom_target('sphinx',
		input : [ sphinx_conf, dst_rsts ],
		output : 'html',
		command : [ cmd_sphinx, '-b', 'html', '-q', '-n',
				join_paths(project_build_root, 'doc'),
				join_paths(project_build_root, 'doc', 'html') ],
                build_by_default : true,
		install : false)

endif
