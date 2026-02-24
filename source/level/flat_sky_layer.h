/******************************************************************************
 * @file    flat_sky_layer.h
 * @brief   flat sky layer
 *****************************************************************************/

#pragma once

/*
================================================================================
 FlatSkyLayer
================================================================================
*/
class FlatSkyLayer {
public:

	struct fsl_update_params_s {
		int				layer_no_;
		float			delta_seconds_;
		const view_define_s *	vd_;
		vert_flat_sky_layer_s *	vb_;
		float			flat_sky_fog_amount_;
		float			flat_sky_z_pos_;
		float			flat_sky_distance_;
	};

	FlatSkyLayer();
	~FlatSkyLayer();

	void					Setup(int layer_no, IRenderResLoader* render_res_loader, const char * tex_file, const glm::vec4& color, float scale, float x_speed, float y_speed);
	void					Reset();

	// return: true: visible, false: invisible
	bool					Update(const fsl_update_params_s& params);

private:

	bool					valid_;
	glm::vec4				color_;
	float					scale_;
	float					x_speed_;
	float					y_speed_;

	float					cur_x_;
	float					cur_y_;

};
