#ifdef FREETYPE_ENABLED
#include "dynamic_font.h"
#include "os/file_access.h"



Ref<DynamicFontAtSize> DynamicFontData::_get_dynamic_font_at_size(int p_size) {


	if (size_cache.has(p_size)) {
		return Ref<DynamicFontAtSize>( size_cache[p_size] );
	}


	Ref<DynamicFontAtSize> dfas;

	dfas.instance();

	dfas->font=Ref<DynamicFontData>( this );

	size_cache[p_size]=dfas.ptr();

	dfas->size=p_size;
	dfas->_load();

	return dfas;

}

void DynamicFontData::set_font_ptr(const uint8_t* p_font_mem,int p_font_mem_size) {

	font_mem=p_font_mem;
	font_mem_size=p_font_mem_size;
}

void DynamicFontData::set_font_path(const String& p_path) {

	font_path=p_path;
}

void DynamicFontData::set_force_autohinter(bool p_force) {

	force_autohinter=p_force;
}

DynamicFontData::DynamicFontData()
{

	force_autohinter=false;
	font_mem=NULL;
	font_mem_size=0;
}

DynamicFontData::~DynamicFontData()
{

}



////////////////////

Error DynamicFontAtSize::_load() {


	int error = FT_Init_FreeType( &library );

	ERR_EXPLAIN(TTR("Error initializing FreeType."));
	ERR_FAIL_COND_V( error !=0, ERR_CANT_CREATE );

	if (font->font_path!=String()) {

		FileAccess *f=FileAccess::open(font->font_path,FileAccess::READ);
		ERR_FAIL_COND_V(!f,ERR_CANT_OPEN);

		memset(&stream,0,sizeof(FT_StreamRec));
		stream.base=NULL;
		stream.size=f->get_len();
		stream.pos=0;
		stream.descriptor.pointer=f;
		stream.read=_ft_stream_io;
		stream.close=_ft_stream_close;

		FT_Open_Args fargs;
		memset(&fargs,0,sizeof(FT_Open_Args));
		fargs.flags=FT_OPEN_STREAM;
		fargs.stream=&stream;
		error = FT_Open_Face( library,&fargs,0,&face);
	} else if (font->font_mem) {

		memset(&stream,0,sizeof(FT_StreamRec));
		stream.base=(unsigned char*)font->font_mem;
		stream.size=font->font_mem_size;
		stream.pos=0;

		FT_Open_Args fargs;
		memset(&fargs,0,sizeof(FT_Open_Args));
		fargs.memory_base=(unsigned char*)font->font_mem;
		fargs.memory_size=font->font_mem_size;
		fargs.flags= FT_OPEN_MEMORY;
		fargs.stream=&stream;
		error = FT_Open_Face( library,&fargs,0,&face);

	} else {
		ERR_EXPLAIN("DynamicFont uninitialized");
		ERR_FAIL_V(ERR_UNCONFIGURED);
	}

	//error = FT_New_Face( library, src_path.utf8().get_data(),0,&face );

	if ( error == FT_Err_Unknown_File_Format ) {
		ERR_EXPLAIN(TTR("Unknown font format."));
		FT_Done_FreeType( library );

	} else if ( error ) {

		ERR_EXPLAIN(TTR("Error loading font."));
		FT_Done_FreeType( library );

	}

	ERR_FAIL_COND_V(error,ERR_FILE_CANT_OPEN);


	/*error = FT_Set_Char_Size(face,0,64*size,512,512);

	if ( error ) {
		FT_Done_FreeType( library );
		ERR_EXPLAIN(TTR("Invalid font size."));
		ERR_FAIL_COND_V( error, ERR_INVALID_PARAMETER );
	}*/

	error = FT_Set_Pixel_Sizes(face,0,size);

	ascent=face->size->metrics.ascender>>6;
	descent=-face->size->metrics.descender>>6;
	linegap=0;

	//print_line("ASCENT: "+itos(ascent)+" descent "+itos(descent)+" hinted: "+itos(face->face_flags&FT_FACE_FLAG_HINTER));

	valid=true;
	return OK;
}

float DynamicFontAtSize::get_height() const {

	return ascent+descent;
}

float DynamicFontAtSize::get_ascent() const {

	return ascent;
}
float DynamicFontAtSize::get_descent() const {

	return descent;
}

Size2 DynamicFontAtSize::get_char_size(CharType p_char,CharType p_next,const Vector<Ref<DynamicFontAtSize> >& p_fallbacks) const {

	if (!valid)
		return Size2(1,1);
	const_cast<DynamicFontAtSize*>(this)->_update_char(p_char);

	const Character *c = char_map.getptr(p_char);
	ERR_FAIL_COND_V(!c,Size2());

	Size2 ret(0,get_height());

	if (!c->found) {

		//not found, try in fallbacks
		for(int i=0;i<p_fallbacks.size();i++) {

			DynamicFontAtSize *fb = const_cast<DynamicFontAtSize*>(p_fallbacks[i].ptr());
			if (!fb->valid)
				continue;

			fb->_update_char(p_char);
			const Character *ch = fb->char_map.getptr(p_char);
			ERR_CONTINUE(!ch);

			if (!ch->found)
				continue;

			c=ch;
			break;
		}
		//not found, try 0xFFFD to display 'not found'.

		if (!c->found) {

			const_cast<DynamicFontAtSize*>(this)->_update_char(0xFFFD);
			c = char_map.getptr(0xFFFD);
			ERR_FAIL_COND_V(!c,Size2());

		}
	}

	if (c->found) {
		ret.x=c->advance;
	}


	if (p_next) {
		FT_Vector  delta;
		FT_Get_Kerning( face, p_char,p_next,  FT_KERNING_DEFAULT, &delta );

		if (delta.x==0) {
			for(int i=0;i<p_fallbacks.size();i++) {

				DynamicFontAtSize *fb = const_cast<DynamicFontAtSize*>(p_fallbacks[i].ptr());
				if (!fb->valid)
					continue;

				FT_Get_Kerning( fb->face, p_char,p_next,  FT_KERNING_DEFAULT, &delta );

				if (delta.x==0)
					continue;

				ret.x+=delta.x>>6;
				break;
			}
		} else {
			ret.x+=delta.x>>6;
		}


	}

	return ret;
}


float DynamicFontAtSize::draw_char(RID p_canvas_item, const Point2& p_pos, const CharType& p_char,const CharType& p_next,const Color& p_modulate,const Vector<Ref<DynamicFontAtSize> >& p_fallbacks) const {

	if (!valid)
		return 0;

	const_cast<DynamicFontAtSize*>(this)->_update_char(p_char);

	const Character * c = char_map.getptr(p_char);

	float advance=0;

	if (!c->found) {

		//not found, try in fallbacks
		bool used_fallback=false;

		for(int i=0;i<p_fallbacks.size();i++) {

			DynamicFontAtSize *fb = const_cast<DynamicFontAtSize*>(p_fallbacks[i].ptr());
			if (!fb->valid)
				continue;

			fb->_update_char(p_char);
			const Character *ch = fb->char_map.getptr(p_char);
			ERR_CONTINUE(!ch);

			if (!ch->found)
				continue;

			Point2 cpos=p_pos;
			cpos.x+=ch->h_align;
			cpos.y-=get_ascent();
			cpos.y+=ch->v_align;
			ERR_FAIL_COND_V( ch->texture_idx<-1 || ch->texture_idx>=fb->textures.size(),0);
			if (ch->texture_idx!=-1)
				VisualServer::get_singleton()->canvas_item_add_texture_rect_region( p_canvas_item, Rect2( cpos, ch->rect.size ), fb->textures[ch->texture_idx].texture->get_rid(),ch->rect, p_modulate );
			advance=ch->advance;
			used_fallback=true;
			break;
		}
		//not found, try 0xFFFD to display 'not found'.

		if (!used_fallback) {

			const_cast<DynamicFontAtSize*>(this)->_update_char(0xFFFD);
			c = char_map.getptr(0xFFFD);

		}
	}

	if (c->found) {


		Point2 cpos=p_pos;
		cpos.x+=c->h_align;
		cpos.y-=get_ascent();
		cpos.y+=c->v_align;
		ERR_FAIL_COND_V( c->texture_idx<-1 || c->texture_idx>=textures.size(),0);
		if (c->texture_idx!=-1)
			VisualServer::get_singleton()->canvas_item_add_texture_rect_region( p_canvas_item, Rect2( cpos, c->rect.size ), textures[c->texture_idx].texture->get_rid(),c->rect, p_modulate );
		advance=c->advance;
		//textures[c->texture_idx].texture->draw(p_canvas_item,Vector2());
	}


	if (p_next) {

		FT_Vector  delta;
		FT_Get_Kerning( face, p_char,p_next,  FT_KERNING_DEFAULT, &delta );

		if (delta.x==0) {
			for(int i=0;i<p_fallbacks.size();i++) {

				DynamicFontAtSize *fb = const_cast<DynamicFontAtSize*>(p_fallbacks[i].ptr());
				if (!fb->valid)
					continue;

				FT_Get_Kerning( fb->face, p_char,p_next,  FT_KERNING_DEFAULT, &delta );

				if (delta.x==0)
					continue;

				advance+=delta.x>>6;
				break;
			}
		} else {
			advance+=delta.x>>6;
		}
	}

	return advance;
}

unsigned long DynamicFontAtSize::_ft_stream_io(FT_Stream      stream,  unsigned long   offset,  unsigned char*  buffer,  unsigned long   count ) {


	FileAccess *f=(FileAccess*)stream->descriptor.pointer;

	if (f->get_pos()!=offset) {
		f->seek(offset);

	}

	if (count==0)
		return 0;

	return f->get_buffer(buffer,count);
}
void DynamicFontAtSize::_ft_stream_close(FT_Stream stream) {

	FileAccess *f=(FileAccess*)stream->descriptor.pointer;
	f->close();
	memdelete(f);
}


void DynamicFontAtSize::_update_char(CharType p_char) {

	if (char_map.has(p_char))
		return;

	_THREAD_SAFE_METHOD_

	FT_GlyphSlot slot = face->glyph;

	if (FT_Get_Char_Index( face, p_char)==0) {
		//not found
		Character ch;
		ch.texture_idx=-1;
		ch.advance=0;
		ch.h_align=0;
		ch.v_align=0;
		ch.found=false;

		char_map[p_char]=ch;
		return;
	}
	int error = FT_Load_Char( face, p_char, FT_LOAD_RENDER|(font->force_autohinter?FT_LOAD_FORCE_AUTOHINT:0) );
	if (!error) {
		error = FT_Render_Glyph( face->glyph, ft_render_mode_normal );
	}
	if (error) {

		int advance=0;
		//stbtt_GetCodepointHMetrics(&font->info, p_char, &advance, 0);
		//print_line("char has no bitmap: "+itos(p_char)+" but advance is "+itos(advance*scale));
		Character ch;
		ch.texture_idx=-1;
		ch.advance=advance;
		ch.h_align=0;
		ch.v_align=0;
		ch.found=false;

		char_map[p_char]=ch;


		return;
	}



	int w = slot->bitmap.width;
	int h = slot->bitmap.rows;
	int p = slot->bitmap.pitch;
	int yofs=slot->bitmap_top;
	int xofs=slot->bitmap_left;
	int advance=slot->advance.x>>6;


	int mw=w+rect_margin*2;
	int mh=h+rect_margin*2;

	if (mw>4096 || mh>4096) {

		ERR_FAIL_COND(mw>4096);
		ERR_FAIL_COND(mh>4096);
	}

	//find a texture to fit this...

	int tex_index=-1;
	int tex_x=0;
	int tex_y=0;

	for(int i=0;i<textures.size();i++) {

		CharTexture &ct=textures[i];

		if (mw > ct.texture_size || mh > ct.texture_size) //too big for this texture
			continue;

		tex_y=0x7FFFFFFF;
		tex_x=0;

		for(int j=0;j<ct.texture_size-mw;j++) {

			int max_y=0;

			for(int k=j;k<j+mw;k++) {

				int y = ct.offsets[k];
				if (y>max_y)
					max_y=y;
			}

			if (max_y<tex_y) {
				tex_y=max_y;
				tex_x=j;
			}
		}

		if (tex_y==0x7FFFFFFF || tex_y+mh > ct.texture_size)
			continue; //fail, could not fit it here

		tex_index=i;
		break;
	}

//	print_line("CHAR: "+String::chr(p_char)+" TEX INDEX: "+itos(tex_index)+" X: "+itos(tex_x)+" Y: "+itos(tex_y));

	if (tex_index==-1) {
		//could not find texture to fit, create one
		tex_x = 0;
		tex_y = 0;

		int texsize = MAX(size*8,256);
		if (mw>texsize)
			texsize=mw; //special case, adapt to it?
		if (mh>texsize)
			texsize=mh; //special case, adapt to it?

		texsize=nearest_power_of_2(texsize);

		texsize=MIN(texsize,4096);


		CharTexture tex;
		tex.texture_size=texsize;
		tex.imgdata.resize(texsize*texsize*2); //grayscale alpha

		{
			//zero texture
			DVector<uint8_t>::Write w = tex.imgdata.write();
			ERR_FAIL_COND(texsize*texsize*2 > tex.imgdata.size());
			for(int i=0;i<texsize*texsize*2;i++) {
				w[i]=0;
			}
		}
		tex.offsets.resize(texsize);
		for(int i=0;i<texsize;i++) //zero offsets
			tex.offsets[i]=0;

		textures.push_back(tex);
		tex_index=textures.size()-1;

	}


	//fit character in char texture

	CharTexture &tex=textures[tex_index];

	{
		DVector<uint8_t>::Write wr = tex.imgdata.write();


		for(int i=0;i<h;i++) {
			for(int j=0;j<w;j++) {

				int ofs = ( (i+tex_y+rect_margin)*tex.texture_size+j+tex_x+rect_margin)*2;
				ERR_FAIL_COND(ofs >= tex.imgdata.size());
				wr[ofs+0]=255; //grayscale as 1
				wr[ofs+1]=slot->bitmap.buffer[i*slot->bitmap.width+j];
			}
		}
	}

	//blit to image and texture
	{

		Image img(tex.texture_size,tex.texture_size,0,Image::FORMAT_GRAYSCALE_ALPHA,tex.imgdata);

		if (tex.texture.is_null()) {
			tex.texture.instance();
			tex.texture->create_from_image(img,0/*Texture::FLAG_FILTER*/);
		} else {
			tex.texture->set_data(img); //update
		}

	}


	// update height array

	for(int k=tex_x;k<tex_x+mw;k++) {

		tex.offsets[k]=tex_y+mh;
	}


	Character chr;
	chr.h_align=xofs;
	chr.v_align=ascent-yofs;// + ascent - descent;
	chr.advance=advance;
	chr.texture_idx=tex_index;
	chr.found=true;


	chr.rect=Rect2(tex_x+rect_margin,tex_y+rect_margin,w,h);

	//print_line("CHAR: "+String::chr(p_char)+" TEX INDEX: "+itos(tex_index)+" RECT: "+chr.rect+" X OFS: "+itos(xofs)+" Y OFS: "+itos(yofs));

	char_map[p_char]=chr;


}

DynamicFontAtSize::DynamicFontAtSize() {

	valid=false;
	rect_margin=1;
	ascent=1;
	descent=1;
	linegap=1;
}

DynamicFontAtSize::~DynamicFontAtSize(){

	if (valid) {
		FT_Done_FreeType( library );
		font->size_cache.erase(size);
	}
}

/////////////////////////



void DynamicFont::set_font_data(const Ref<DynamicFontData>& p_data) {

	data=p_data;
	data_at_size=data->_get_dynamic_font_at_size(size);
	emit_changed();
}


Ref<DynamicFontData> DynamicFont::get_font_data() const{

	return data;
}

void DynamicFont::set_size(int p_size){

	if (size==p_size)
		return;
	size=p_size;
	ERR_FAIL_COND(p_size<1);
	if (!data.is_valid())
		return;
	data_at_size=data->_get_dynamic_font_at_size(size);
	for(int i=0;i<fallbacks.size();i++) {
		fallback_data_at_size[i]=fallbacks[i]->_get_dynamic_font_at_size(size);
	}

	emit_changed();
	_change_notify();
}
int DynamicFont::get_size() const{

	return size;
}

float DynamicFont::get_height() const{

	if (!data_at_size.is_valid())
		return 1;

	return data_at_size->get_height();
}

float DynamicFont::get_ascent() const{

	if (!data_at_size.is_valid())
		return 1;

	return data_at_size->get_ascent();
}

float DynamicFont::get_descent() const{

	if (!data_at_size.is_valid())
		return 1;

	return data_at_size->get_descent();

}

Size2 DynamicFont::get_char_size(CharType p_char,CharType p_next) const{

	if (!data_at_size.is_valid())
		return Size2(1,1);

	return data_at_size->get_char_size(p_char,p_next,fallback_data_at_size);

}

bool DynamicFont::is_distance_field_hint() const{

	return false;
}

float DynamicFont::draw_char(RID p_canvas_item, const Point2& p_pos, const CharType& p_char,const CharType& p_next,const Color& p_modulate) const {

	if (!data_at_size.is_valid())
		return 0;

	return data_at_size->draw_char(p_canvas_item,p_pos,p_char,p_next,p_modulate,fallback_data_at_size);

}
void DynamicFont::set_fallback(int p_idx,const Ref<DynamicFontData>& p_data) {

	ERR_FAIL_COND(p_data.is_null());
	ERR_FAIL_INDEX(p_idx,fallbacks.size());
	fallbacks[p_idx]=p_data;
	fallback_data_at_size[p_idx]=fallbacks[p_idx]->_get_dynamic_font_at_size(size);

}

void DynamicFont::add_fallback(const Ref<DynamicFontData>& p_data) {

	ERR_FAIL_COND(p_data.is_null());
	fallbacks.push_back(p_data);
	fallback_data_at_size.push_back(fallbacks[fallbacks.size()-1]->_get_dynamic_font_at_size(size)); //const..

	_change_notify();
	emit_changed();
	_change_notify();

}

int DynamicFont::get_fallback_count() const {
	return fallbacks.size();
}
Ref<DynamicFontData> DynamicFont::get_fallback(int p_idx) const {

	ERR_FAIL_INDEX_V(p_idx,fallbacks.size(),Ref<DynamicFontData>());

	return fallbacks[p_idx];
}
void DynamicFont::remove_fallback(int p_idx) {

	ERR_FAIL_INDEX(p_idx,fallbacks.size());
	fallbacks.remove(p_idx);
	fallback_data_at_size.remove(p_idx);
	emit_changed();
	_change_notify();
}

bool DynamicFont::_set(const StringName& p_name, const Variant& p_value) {

	String str = p_name;
	if (str.begins_with("fallback/")) {
		int idx = str.get_slicec('/',1).to_int();
		Ref<DynamicFontData> fd = p_value;

		if (fd.is_valid()) {
			if (idx==fallbacks.size()) {
				add_fallback(fd);
				return true;
			} else if (idx>=0 && idx<fallbacks.size()) {
				set_fallback(idx,fd);
				return true;
			} else {
				return false;
			}
		} else if (idx>=0 && idx<fallbacks.size()) {
			remove_fallback(idx);
			return true;
		}
	}

	return false;
}

bool DynamicFont::_get(const StringName& p_name,Variant &r_ret) const{

	String str = p_name;
	if (str.begins_with("fallback/")) {
		int idx = str.get_slicec('/',1).to_int();

		if (idx==fallbacks.size()) {
			r_ret=Ref<DynamicFontData>();
			return true;
		} else if (idx>=0 && idx<fallbacks.size()) {
			r_ret=get_fallback(idx);
			return true;
		}
	}

	return false;
}
void DynamicFont::_get_property_list( List<PropertyInfo> *p_list) const{

	for(int i=0;i<fallbacks.size();i++) {
		p_list->push_back(PropertyInfo(Variant::OBJECT,"fallback/"+itos(i),PROPERTY_HINT_RESOURCE_TYPE,"DynamicFontData"));
	}

	p_list->push_back(PropertyInfo(Variant::OBJECT,"fallback/"+itos(fallbacks.size()),PROPERTY_HINT_RESOURCE_TYPE,"DynamicFontData"));
}


void DynamicFont::_bind_methods() {

	ObjectTypeDB::bind_method(_MD("set_font_data","data:DynamicFontData"),&DynamicFont::set_font_data);
	ObjectTypeDB::bind_method(_MD("get_font_data:DynamicFontData"),&DynamicFont::get_font_data);

	ObjectTypeDB::bind_method(_MD("set_size","data"),&DynamicFont::set_size);
	ObjectTypeDB::bind_method(_MD("get_size"),&DynamicFont::get_size);

	ObjectTypeDB::bind_method(_MD("add_fallback","data:DynamicFontData"),&DynamicFont::add_fallback);
	ObjectTypeDB::bind_method(_MD("set_fallback","idx","data:DynamicFontData"),&DynamicFont::set_fallback);
	ObjectTypeDB::bind_method(_MD("get_fallback:DynamicFontData","idx"),&DynamicFont::get_fallback);
	ObjectTypeDB::bind_method(_MD("remove_fallback","idx"),&DynamicFont::remove_fallback);
	ObjectTypeDB::bind_method(_MD("get_fallback_count"),&DynamicFont::get_fallback_count);


	ADD_PROPERTY(PropertyInfo(Variant::INT,"font/size"),_SCS("set_size"),_SCS("get_size"));
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT,"font/font",PROPERTY_HINT_RESOURCE_TYPE,"DynamicFontData"),_SCS("set_font_data"),_SCS("get_font_data"));
}

DynamicFont::DynamicFont() {

	size=16;
}

DynamicFont::~DynamicFont() {

}

/////////////////////////


RES ResourceFormatLoaderDynamicFont::load(const String &p_path, const String& p_original_path, Error *r_error) {

	if (r_error)
		*r_error=ERR_FILE_CANT_OPEN;

	Ref<DynamicFontData> dfont;
	dfont.instance();;
	dfont->set_font_path(p_path);


	if (r_error)
		*r_error=OK;

	return dfont;
}

void ResourceFormatLoaderDynamicFont::get_recognized_extensions(List<String> *p_extensions) const {

	p_extensions->push_back("ttf");
	p_extensions->push_back("otf");
}

bool ResourceFormatLoaderDynamicFont::handles_type(const String& p_type) const {

	return (p_type=="DynamicFontData");
}

String ResourceFormatLoaderDynamicFont::get_resource_type(const String &p_path) const {

	String el = p_path.extension().to_lower();
	if (el=="ttf" || el=="otf")
		return "DynamicFontData";
	return "";
}


#endif
