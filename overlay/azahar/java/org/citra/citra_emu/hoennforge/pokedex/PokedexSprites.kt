// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.drawable.Drawable
import android.widget.ImageView
import java.io.IOException
import org.citra.citra_emu.utils.Log

/**
 * Offline sprites: assets/pokedex/sprites/{id}.png (classic ~96px, bundled in APK).
 */
object PokedexSprites {
    private const val ASSET_DIR = "pokedex/sprites"

    fun assetPath(id: Int): String = "$ASSET_DIR/$id.png"

    fun loadAssetBitmap(context: Context, id: Int): Bitmap? {
        return try {
            context.assets.open(assetPath(id)).use { stream ->
                BitmapFactory.decodeStream(stream)
            }
        } catch (_: IOException) {
            null
        } catch (e: Exception) {
            Log.warning("[Pokedex] asset sprite $id failed: $e")
            null
        }
    }

    fun bind(imageView: ImageView, id: Int, placeholder: Drawable?) {
        val ctx = imageView.context.applicationContext ?: imageView.context
        val bmp = loadAssetBitmap(ctx, id)
        if (bmp != null) {
            imageView.setImageBitmap(bmp)
            imageView.scaleType = ImageView.ScaleType.FIT_CENTER
            imageView.adjustViewBounds = true
            return
        }
        Log.warning("[Pokedex] missing asset sprite id=$id path=${assetPath(id)}")
        if (placeholder != null) {
            imageView.setImageDrawable(placeholder)
        }
    }
}
