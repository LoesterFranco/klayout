
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2019 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "dbLayoutToNetlist.h"
#include "dbLayoutToNetlistWriter.h"
#include "dbStream.h"
#include "dbCommonReader.h"
#include "dbNetlistDeviceExtractorClasses.h"

#include "tlUnitTest.h"
#include "tlStream.h"
#include "tlFileUtils.h"

static unsigned int define_layer (db::Layout &ly, db::LayerMap &lmap, int gds_layer, int gds_datatype = 0)
{
  unsigned int lid = ly.insert_layer (db::LayerProperties (gds_layer, gds_datatype));
  lmap.map (ly.get_properties (lid), lid);
  return lid;
}

TEST(1_WriterBasic)
{
  db::Layout ly;
  db::LayerMap lmap;

  unsigned int nwell      = define_layer (ly, lmap, 1);
  unsigned int active     = define_layer (ly, lmap, 2);
  unsigned int poly       = define_layer (ly, lmap, 3);
  unsigned int poly_lbl   = define_layer (ly, lmap, 3, 1);
  unsigned int diff_cont  = define_layer (ly, lmap, 4);
  unsigned int poly_cont  = define_layer (ly, lmap, 5);
  unsigned int metal1     = define_layer (ly, lmap, 6);
  unsigned int metal1_lbl = define_layer (ly, lmap, 6, 1);
  unsigned int via1       = define_layer (ly, lmap, 7);
  unsigned int metal2     = define_layer (ly, lmap, 8);
  unsigned int metal2_lbl = define_layer (ly, lmap, 8, 1);

  {
    db::LoadLayoutOptions options;
    options.get_options<db::CommonReaderOptions> ().layer_map = lmap;
    options.get_options<db::CommonReaderOptions> ().create_other_layers = false;

    std::string fn (tl::testsrc ());
    fn = tl::combine_path (fn, "testdata");
    fn = tl::combine_path (fn, "algo");
    fn = tl::combine_path (fn, "device_extract_l1.gds");

    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly, options);
  }

  db::Cell &tc = ly.cell (*ly.begin_top_down ());
  db::LayoutToNetlist l2n (db::RecursiveShapeIterator (ly, tc, std::set<unsigned int> ()));

  std::auto_ptr<db::Region> rnwell (l2n.make_layer (nwell, "nwell"));
  std::auto_ptr<db::Region> ractive (l2n.make_layer (active, "active"));
  std::auto_ptr<db::Region> rpoly (l2n.make_polygon_layer (poly, "poly"));
  std::auto_ptr<db::Region> rpoly_lbl (l2n.make_text_layer (poly_lbl, "poly_lbl"));
  std::auto_ptr<db::Region> rdiff_cont (l2n.make_polygon_layer (diff_cont, "diff_cont"));
  std::auto_ptr<db::Region> rpoly_cont (l2n.make_polygon_layer (poly_cont, "poly_cont"));
  std::auto_ptr<db::Region> rmetal1 (l2n.make_polygon_layer (metal1, "metal1"));
  std::auto_ptr<db::Region> rmetal1_lbl (l2n.make_text_layer (metal1_lbl, "metal1_lbl"));
  std::auto_ptr<db::Region> rvia1 (l2n.make_polygon_layer (via1, "via1"));
  std::auto_ptr<db::Region> rmetal2 (l2n.make_polygon_layer (metal2, "metal2"));
  std::auto_ptr<db::Region> rmetal2_lbl (l2n.make_text_layer (metal2_lbl, "metal2_lbl"));

  //  derived regions

  db::Region rpactive = *ractive & *rnwell;
  db::Region rpgate   = rpactive & *rpoly;
  db::Region rpsd     = rpactive - rpgate;
  l2n.name (rpactive, "pactive");
  l2n.name (rpgate,   "pgate");
  l2n.name (rpsd,     "psd");

  db::Region rnactive = *ractive - *rnwell;
  db::Region rngate   = rnactive & *rpoly;
  db::Region rnsd     = rnactive - rngate;
  l2n.name (rnactive, "nactive");
  l2n.name (rngate,   "ngate");
  l2n.name (rnsd,     "nsd");

  db::NetlistDeviceExtractorMOS3Transistor pmos_ex ("PMOS");
  db::NetlistDeviceExtractorMOS3Transistor nmos_ex ("NMOS");

  //  device extraction

  db::NetlistDeviceExtractor::input_layers dl;

  dl["SD"] = &rpsd;
  dl["G"] = &rpgate;
  dl["P"] = rpoly.get ();  //  not needed for extraction but to return terminal shapes
  l2n.extract_devices (pmos_ex, dl);

  dl["SD"] = &rnsd;
  dl["G"] = &rngate;
  dl["P"] = rpoly.get ();  //  not needed for extraction but to return terminal shapes
  l2n.extract_devices (nmos_ex, dl);

  //  return the computed layers into the original layout and write it for debugging purposes
  //  NOTE: this will include the device layers too

  unsigned int lgate  = ly.insert_layer (db::LayerProperties (10, 0));      // 10/0 -> Gate
  unsigned int lsd    = ly.insert_layer (db::LayerProperties (11, 0));      // 11/0 -> Source/Drain
  unsigned int lpdiff = ly.insert_layer (db::LayerProperties (12, 0));      // 12/0 -> P Diffusion
  unsigned int lndiff = ly.insert_layer (db::LayerProperties (13, 0));      // 13/0 -> N Diffusion
  unsigned int lpoly  = ly.insert_layer (db::LayerProperties (14, 0));      // 14/0 -> Poly with gate terminal

  rpgate.insert_into (&ly, tc.cell_index (), lgate);
  rngate.insert_into (&ly, tc.cell_index (), lgate);
  rpsd.insert_into (&ly, tc.cell_index (), lsd);
  rnsd.insert_into (&ly, tc.cell_index (), lsd);
  rpsd.insert_into (&ly, tc.cell_index (), lpdiff);
  rnsd.insert_into (&ly, tc.cell_index (), lndiff);
  rpoly->insert_into (&ly, tc.cell_index (), lpoly);

  //  net extraction

  //  Intra-layer
  l2n.connect (rpsd);
  l2n.connect (rnsd);
  l2n.connect (*rpoly);
  l2n.connect (*rdiff_cont);
  l2n.connect (*rpoly_cont);
  l2n.connect (*rmetal1);
  l2n.connect (*rvia1);
  l2n.connect (*rmetal2);
  //  Inter-layer
  l2n.connect (rpsd,        *rdiff_cont);
  l2n.connect (rnsd,        *rdiff_cont);
  l2n.connect (*rpoly,      *rpoly_cont);
  l2n.connect (*rpoly_cont, *rmetal1);
  l2n.connect (*rdiff_cont, *rmetal1);
  l2n.connect (*rmetal1,    *rvia1);
  l2n.connect (*rvia1,      *rmetal2);
  l2n.connect (*rpoly,      *rpoly_lbl);     //  attaches labels
  l2n.connect (*rmetal1,    *rmetal1_lbl);   //  attaches labels
  l2n.connect (*rmetal2,    *rmetal2_lbl);   //  attaches labels

  //  create some mess - we have to keep references to the layers to make them not disappear
  rmetal1_lbl.reset (0);
  rmetal2_lbl.reset (0);
  rpoly_lbl.reset (0);

  l2n.extract_netlist ();
  l2n.netlist ()->purge ();

  std::string path = tmp_file ("tmp_l2nwriter_1.txt");
  {
    tl::OutputStream stream (path);
    db::LayoutToNetlistStandardWriter writer (stream);
    writer.write (&l2n);
  }

  std::string au_path = tl::combine_path (tl::combine_path (tl::combine_path (tl::testsrc (), "testdata"), "algo"), "l2n_writer_au.txt");

  tl::InputStream is (path);
  tl::InputStream is_au (au_path);

  if (is.read_all () != is_au.read_all ()) {
    _this->raise (tl::sprintf ("Compare failed - see\n  actual: %s\n  golden: %s",
                               tl::absolute_file_path (path),
                               tl::absolute_file_path (au_path)));
  }
}
