if get_option('documentation')

cmd_sphinx = find_program('sphinx-build-3')
cmd_doxygen = find_program('doxygen')
cmd_xsltproc = find_program('xsltproc')

doc_config = configuration_data()
doc_config.set('PACKAGE_NAME', meson.project_name())
doc_config.set('PACKAGE_VERSION', meson.project_version())
doc_config.set('top_srcdir', meson.source_root())

# We have a dependency chain here:
# - generate doxygen xml from the source
# - convert the xml to rst with xstlproc
# - generate the sphinx conf
# - run sphinx against the generated rst
doxyfile = configure_file(input : 'libratbag.doxygen.in',
                          output : 'libratbag.doxygen',
                          configuration : doc_config,
                          install : false)

src_doxygen = [ doxyfile,
                files('../src/libratbag-enums.h') ]
doxygen = custom_target('doxygen',
			input : src_doxygen,
			output : [ 'xml' ],
			command : [ cmd_doxygen, doxyfile ],
			install : false,
			build_by_default : true)


# Sphinx really really wants a _static directory in its source tree. Let's
# make it happy
run_command('mkdir', '-p', join_paths(meson.build_root(), 'doc', '_static'))

xsl = join_paths(meson.source_root(), 'doc', 'doxygen-enums-to-rst.xsl')
src_xsltproc = join_paths(meson.build_root(), 'xml', 'group__enums.xml')
dst_xstlproc = join_paths(meson.build_root(), 'doc', 'enums.rst')
rst_enum = custom_target('xsltproc',
			 input : doxygen,
			 output : 'enums.rst',
			 command: [ cmd_xsltproc, '-o', dst_xstlproc, xsl, src_xsltproc],
			 build_by_default : true)

# Effectively a noop copy. We *must* generate enums.rst so that will end up
# in the build directory. sphinx doesn't allow for multiple source
# directories, so any .rst and the conf.py must be copied over to the
# build directory too
src_rsts = ['index.rst', 'dbus.rst']
dst_rsts = [rst_enum]
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
				join_paths(meson.build_root(), 'doc'),
				join_paths(meson.build_root(), 'doc', 'html') ],
                build_by_default : true,
		install : false)

endif
