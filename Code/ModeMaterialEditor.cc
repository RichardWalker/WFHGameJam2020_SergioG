void
materialSliders(MaterialConstantsCB* matc)
{
   float baseX = 50;
   float col1 = baseX + 100;
   float slWidth = 250;
   immSetCursor(baseX, 150);
   // First column
   {
      gUI->cursor.x = baseX;
      immText("albedo R", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col1;
      immSlider(&matc->albedo.r, 0, 1, slWidth);
   }
   {
      gUI->cursor.x = baseX;
      immText("albedo G", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col1;
      immSlider(&matc->albedo.g, 0, 1, slWidth);
   }
   {
      gUI->cursor.x = baseX;
      immText("albedo B", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col1;
      immSlider(&matc->albedo.b, 0, 1, slWidth);
   }
   {
      gUI->cursor.x = baseX;
      immText("all albedo", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col1;
      vec3 albedo = matc->albedo.rgb;

      static float slider;
      slider = Min(albedo.r, Min(albedo.g, albedo.b));
      if (immSlider(&slider, 0, 1, slWidth)) {
         float diff = slider - Min(albedo.r, Min(albedo.g, albedo.b));
         matc->albedo.rgb += { diff, diff,  diff };
         matc->albedo = saturate(matc->albedo);
      }
   }

   // Second column
   float secondX = 450;
   float col2 = secondX + 100;
   immSetCursor(secondX, 150);
   {
      gUI->cursor.x = secondX;
      immText("specular R", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col2;
      immSlider(&matc->specularColor.r, 0, 1, slWidth);
   }
   {
      gUI->cursor.x = secondX;
      immText("specular G", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col2;
      immSlider(&matc->specularColor.g, 0, 1, slWidth);
   }
   {
      gUI->cursor.x = secondX;
      immText("specular B", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col2;
      immSlider(&matc->specularColor.b, 0, 1, slWidth);
   }
   {
      float offset = 50;
      gUI->cursor.x = secondX;
      immText("all specular", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col2;

      static float slider;
      vec3 specularColor = matc->specularColor.rgb;
      slider = Min(specularColor.r, Min(specularColor.g, specularColor.b));
      if (immSlider(&slider, 0, 1, slWidth - offset)) {
         float diff = slider - Min(specularColor.r, Min(specularColor.g, specularColor.b));
         matc->specularColor.rgb += { diff, diff,  diff };
         matc->specularColor = saturate(matc->specularColor);
      }
   }
   {
      gUI->cursor.x = secondX;
      immText("roughness", FontSize_Small);
      immSameLine();
      gUI->cursor.x = col2;
      static float rough;
      rough = sqrt(matc->roughness);
      if (immSlider(&rough, 0, 1, slWidth)) {
         matc->roughness = rough*rough;
      }
   }
}