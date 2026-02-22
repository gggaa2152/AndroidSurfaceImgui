// ========== 棋盘（只用圆形包裹图片） ==========
void DrawBoard() {
    if (!g_esp) return;
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    float sz = 40 * g_boardScale;
    float w = 7*sz, h = 4*sz;
    static float x=200,y=200; static bool drag=0;
    
    if (ImGui::IsMouseDown(0)) {
        ImVec2 m = ImGui::GetMousePos();
        if (!drag && m.x>=x && m.x<=x+w && m.y>=y && m.y<=y+h) drag=1;
        if (drag) { x = m.x-w/2; y = m.y-h/2; }
    } else drag=0;
    
    // 绘制棋盘背景
    d->AddRectFilled(ImVec2(x,y), ImVec2(x+w,y+h), 0x1E1E1E64, 4);
    
    // 绘制格子线
    for (int i=0; i<=4; i++) d->AddLine(ImVec2(x,y+i*sz), ImVec2(x+w,y+i*sz), 0x646464FF);
    for (int i=0; i<=7; i++) d->AddLine(ImVec2(x+i*sz,y), ImVec2(x+i*sz,y+h), 0x646464FF);
    
    // ===== 只用圆形包裹图片 =====
    if (g_testTexture) {
        ImTextureID texID = (ImTextureID)(intptr_t)g_testTexture;
        float radius = sz * 0.35f;  // 圆形半径
        
        for (int r=0; r<4; r++) {
            for (int c=0; c<7; c++) {
                float cx = x + c*sz + sz/2;
                float cy = y + r*sz + sz/2;
                
                if (r == 3) {  // 底部格子（第4行）
                    // 绘制圆形背景（半透明黑）
                    d->AddCircleFilled(ImVec2(cx,cy), radius, 0x00000080, 32);
                    
                    // 绘制图片（大小正好适合圆形）
                    float imgSize = radius * 1.4f;  // 图片大小刚好被圆形包裹
                    float imgX = cx - imgSize/2;
                    float imgY = cy - imgSize/2;
                    d->AddImage(texID, 
                               ImVec2(imgX, imgY), 
                               ImVec2(imgX + imgSize, imgY + imgSize));
                    
                    // 绘制白色圆形边框
                    d->AddCircle(ImVec2(cx,cy), radius, 0xFFFFFFFF, 32, 2.0f);
                } else {
                    // 其他格子绘制圆形（红蓝交替）
                    d->AddCircleFilled(ImVec2(cx,cy), sz*0.3, (r+c)%2 ? 0x6464FFFF : 0xFF6464FF, 32);
                    d->AddCircle(ImVec2(cx,cy), sz*0.3, 0xFFFFFF96, 32, 1);
                }
            }
        }
    } else {
        // 没有头像就全部画圆圈
        for (int r=0; r<4; r++) for (int c=0; c<7; c++) {
            float cx = x + c*sz + sz/2, cy = y + r*sz + sz/2;
            d->AddCircleFilled(ImVec2(cx,cy), sz*0.3, (r+c)%2 ? 0x6464FFFF : 0xFF6464FF, 32);
            d->AddCircle(ImVec2(cx,cy), sz*0.3, 0xFFFFFF96, 32, 1);
        }
    }
}
