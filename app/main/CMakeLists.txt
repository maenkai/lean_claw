set(project_top "../..")
set(compent_top "${project_top}/agent" )

set(compent_dirs "." 
"${compent_top}" 
"${compent_top}/utils" 
"${compent_top}/exec" 
"${compent_top}/skill" 
"${compent_top}/llm" "${compent_top}/llm/adapter" 
"${compent_top}/channels/cli" "${compent_top}/channels/feishu"
)

set(project_dirs "." 
"${project_top}/tool" 
"${project_top}/console" 
)


idf_component_register(SRC_DIRS ${project_dirs} ${compent_dirs}  INCLUDE_DIRS ${project_dirs} ${compent_dirs})
target_compile_options(${COMPONENT_TARGET} PRIVATE -Wno-format)
