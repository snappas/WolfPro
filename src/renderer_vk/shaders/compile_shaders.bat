set OPT=-O3
if /i "%1"=="debug" set OPT=-O0 -Zi -Qembed_debug -fspv-debug=vulkan-with-source
set DXC=C:\Users\snapp\Downloads\dxc_2025_05_24\bin\x64\dxc.exe
del *_vs.h
del *_ps.h
del *_ps_trace.h
del *_cs.h

%DXC% %OPT% -spirv -T vs_6_0 -E vs %cd%\generic.hlsl -Fh %cd%\generic_vs.h -Vn generic_vs -D VS
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic.hlsl -Fh %cd%\generic_ps.h -Vn generic_ps -D PS
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic.hlsl -Fh %cd%\generic_ps_at.h -Vn generic_ps_at -D PS -D AT
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic.hlsl -Fh %cd%\generic_ps_at_a2c.h -Vn generic_ps_at_a2c -D PS -D AT -D A2C
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic.hlsl -Fh %cd%\generic_ps_trace.h -Vn generic_ps_trace -D PS -D TRACE
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic.hlsl -Fh %cd%\generic_ps_at_trace.h -Vn generic_ps_at_trace -D PS -D AT -D TRACE
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic.hlsl -Fh %cd%\generic_ps_at_a2c_trace.h -Vn generic_ps_at_a2c_trace -D PS -D AT -D A2C -D TRACE
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic2s.hlsl -Fh %cd%\generic2s_ps_trace.h -Vn generic2s_ps_trace -D PS -D TRACE
%DXC% %OPT% -spirv -T vs_6_0 -E vs %cd%\gamma.hlsl -Fh %cd%\gamma_vs.h -Vn gamma_vs -D VS
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\gamma.hlsl -Fh %cd%\gamma_ps.h -Vn gamma_ps -D PS
%DXC% %OPT% -spirv -T vs_6_0 -E vs %cd%\imgui.hlsl -Fh %cd%\imgui_vs.h -Vn imgui_vs -D VS
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\imgui.hlsl -Fh %cd%\imgui_ps.h -Vn imgui_ps -D PS
%DXC% %OPT% -spirv -T vs_6_0 -E vs %cd%\blit.hlsl -Fh %cd%\blit_vs.h -Vn blit_vs -D VS
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\blit.hlsl -Fh %cd%\blit_ps.h -Vn blit_ps -D PS
%DXC% %OPT% -spirv -T vs_6_0 -E vs %cd%\generic2s.hlsl -Fh %cd%\generic2s_vs.h -Vn generic2s_vs -D VS
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\generic2s.hlsl -Fh %cd%\generic2s_ps.h -Vn generic2s_ps -D PS
%DXC% %OPT% -spirv -T cs_6_0 -E cs %cd%\mipmap.hlsl -Fh %cd%\mipmap_cs.h -Vn mipmap_cs -D CS
%DXC% %OPT% -spirv -T cs_6_0 -E cs %cd%\mipmap_x.hlsl -Fh %cd%\mipmap_x_cs.h -Vn mipmap_x_cs -D CS
%DXC% %OPT% -spirv -T cs_6_0 -E cs %cd%\mipmap_y.hlsl -Fh %cd%\mipmap_y_cs.h -Vn mipmap_y_cs -D CS
%DXC% %OPT% -spirv -T vs_6_0 -E vs %cd%\dynamiclight.hlsl -Fh %cd%\dynamiclight_vs.h -Vn dynamiclight_vs -D VS
%DXC% %OPT% -spirv -T ps_6_0 -E ps %cd%\dynamiclight.hlsl -Fh %cd%\dynamiclight_ps.h -Vn dynamiclight_ps -D PS
%DXC% %OPT% -spirv -T cs_6_0 -E cs %cd%\msaa.hlsl -Fh %cd%\msaa_2_cs.h -Vn msaa_2_cs -D SAMPLE_COUNT=2
%DXC% %OPT% -spirv -T cs_6_0 -E cs %cd%\msaa.hlsl -Fh %cd%\msaa_4_cs.h -Vn msaa_4_cs -D SAMPLE_COUNT=4
%DXC% %OPT% -spirv -T cs_6_0 -E cs %cd%\msaa.hlsl -Fh %cd%\msaa_8_cs.h -Vn msaa_8_cs -D SAMPLE_COUNT=8